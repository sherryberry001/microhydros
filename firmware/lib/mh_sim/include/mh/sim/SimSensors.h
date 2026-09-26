// MicroHydros - simulerade sensorer med felinjektion
//
// De har klasserna implementerar samma ISensor som de riktiga drivrutinerna och
// beter sig som de riktiga kretsarna gor: de brusar, de kvantiserar till sin
// upplosning, de svarar med fordrojning, och de gar sonder pa de satt de faktiskt
// gar sonder pa.
//
// Felmoderna ar inte pahittade. De ar de fel som star i databladen eller som ar
// valkanda for respektive krets:
//
//   Ds18b20PowerOnValue  En DS18B20 med glapp i kabeln eller utan pullup laser ut
//                        +85.0 C, som ar registrets varde efter reset. Det ar den
//                        vanligaste falska matningen i hela 1-Wire-varlden och den
//                        ser ut som ett helt normalt tal.
//   CrcFailure           Bade SHT4x och DS18B20 skickar CRC8. Vid dalig kabel
//                        eller for snabb buss slar den ut.
//   NoResponse           Kretsen svarar inte alls, typiskt vid brott eller
//                        spanningsfall.
//   StuckValue           En I2C-buss som hangt sig ger samma register om och om
//                        igen. SHT4x efter kondens visar last 100 procent RH.
//   Drift                Langsam forskjutning, vad som faktiskt hander med
//                        billiga polymergivare over manader.
//
// Slumpen ar en xorshift32 med fast fro. Det ar medvetet: varje testkorning ger
// exakt samma serie, sa ett fel gar att reproducera och testresultaten i
// dokumentationen gar att kora om och fa samma siffror.

#pragma once

#include "mh/Interfaces.h"
#include "mh/sim/GrowBoxModel.h"

namespace mh {
namespace sim {

enum class FaultMode : uint8_t {
    None = 0,
    CrcFailure,
    NoResponse,
    StuckValue,
    Ds18b20PowerOnValue, ///< fastnar pa +85.0 C
    Drift,
    Spike,
};

/// Deterministisk slumpgenerator. xorshift32, fullt tillracklig for brus och
/// snabb nog att kora miljontals steg pa en sekund.
class Rng {
public:
    explicit Rng(uint32_t seed = 0x1234ABCDu) : s_(seed ? seed : 1u) {}

    uint32_t next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }

    /// Likformigt i [0, 1).
    float uniform() { return static_cast<float>(next() >> 8) / 16777216.0f; }

    /// Ungefar normalfordelat via summan av tolv likformiga, Irwin-Hall.
    /// Billigare an Box-Muller och behover ingen logaritm.
    float gaussian() {
        float sum = 0.0f;
        for (int i = 0; i < 12; ++i) sum += uniform();
        return sum - 6.0f;
    }

    void seed(uint32_t s) { s_ = s ? s : 1u; }

private:
    uint32_t s_;
};

/// Gemensam bas for de simulerade givarna.
class SimSensorBase : public ISensor {
public:
    SimSensorBase(const char* id, Quantity q, Location loc, const GrowBoxModel& model,
                  IClock& clock, Rng& rng);

    const char* id() const override { return id_; }
    Quantity    quantity() const override { return quantity_; }
    Location    location() const override { return location_; }
    bool        begin() override;

    /// Styr felinjektionen. probability ar sannolikheten per avlasning, 1.0 gor
    /// felet permanent.
    void setFault(FaultMode mode, float probability = 1.0f) {
        fault_ = mode;
        fault_probability_ = probability;
    }
    void clearFault() { fault_ = FaultMode::None; }
    FaultMode fault() const { return fault_; }

    void setDriftPerHour(float d) { drift_per_hour_ = d; }

protected:
    /// Sant varde ur modellen, utan brus och utan fel.
    virtual float trueValue() const = 0;
    /// Sensorns brusniva, en standardavvikelse.
    virtual float noiseSigma() const = 0;
    /// Kvantiseringssteg, sensorns upplosning.
    virtual float resolution() const = 0;

    RawReading readWithFaults();

    const char*          id_;
    Quantity             quantity_;
    Location             location_;
    const GrowBoxModel&  model_;
    IClock&              clock_;
    Rng&                 rng_;

    FaultMode fault_;
    float     fault_probability_;
    float     drift_per_hour_;
    float     accumulated_drift_;
    float     last_value_;
    bool      has_last_;
    uint32_t  last_ms_;
    bool      started_;
};

/// Sensirion SHT45, lufttemperatur inne. Datablad: +-0.1 C typiskt, 0.01 C
/// upplosning.
class SimSht45Temperature : public SimSensorBase {
public:
    SimSht45Temperature(const GrowBoxModel& m, IClock& c, Rng& r)
        : SimSensorBase("air_in", Quantity::AirTemperature, Location::Inside, m, c, r) {}
    RawReading read() override { return readWithFaults(); }

protected:
    float trueValue() const override { return model_.state().air_inside_c; }
    float noiseSigma() const override { return 0.04f; }
    float resolution() const override { return 0.01f; }
};

/// Sensirion SHT45, relativ fuktighet inne. Datablad: +-1.0 procent RH,
/// 0.01 procent upplosning.
class SimSht45Humidity : public SimSensorBase {
public:
    SimSht45Humidity(const GrowBoxModel& m, IClock& c, Rng& r)
        : SimSensorBase("rh_in", Quantity::RelativeHumidity, Location::Inside, m, c, r) {}
    RawReading read() override { return readWithFaults(); }

protected:
    float trueValue() const override { return model_.state().rh_inside_percent; }
    float noiseSigma() const override { return 0.35f; }
    float resolution() const override { return 0.01f; }
};

/// Sensirion SHT40-BD1B, lufttemperatur ute. Samma familj men enklare bin,
/// +-0.2 C.
class SimSht40Temperature : public SimSensorBase {
public:
    SimSht40Temperature(const GrowBoxModel& m, IClock& c, Rng& r)
        : SimSensorBase("air_out", Quantity::AirTemperature, Location::Outside, m, c, r) {}
    RawReading read() override { return readWithFaults(); }

protected:
    float trueValue() const override { return model_.state().air_outside_c; }
    float noiseSigma() const override { return 0.08f; }
    float resolution() const override { return 0.01f; }
};

/// Maxim DS18B20 i vattentat rostfri sond. 12 bitars upplosning ger 0.0625 C,
/// noggrannhet +-0.5 C i intervallet -10 till +85 C.
class SimDs18b20 : public SimSensorBase {
public:
    SimDs18b20(const GrowBoxModel& m, IClock& c, Rng& r)
        : SimSensorBase("water", Quantity::WaterTemperature, Location::Water, m, c, r) {}
    RawReading read() override { return readWithFaults(); }

protected:
    float trueValue() const override { return model_.state().water_c; }
    float noiseSigma() const override { return 0.05f; }
    float resolution() const override { return 0.0625f; }
};

/// Klocka som testet stegar sjalvt. Gor testerna deterministiska och gor att en
/// simulerad vecka tar en brakdel av en sekund att kora.
class SimClock : public IClock {
public:
    SimClock() : now_ms_(0) {}
    uint32_t nowMs() const override { return now_ms_; }
    void advance(uint32_t ms) { now_ms_ += ms; }
    void setNow(uint32_t ms) { now_ms_ = ms; }

private:
    uint32_t now_ms_;
};

} // namespace sim
} // namespace mh
