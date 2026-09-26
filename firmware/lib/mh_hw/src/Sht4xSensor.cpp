#ifdef ARDUINO

#include "mh/hw/Sht4xSensor.h"

namespace mh {
namespace hw {

namespace {
constexpr uint8_t kCmdMeasureHighPrecision = 0xFD;
constexpr uint8_t kCmdSoftReset            = 0x94;
constexpr uint8_t kCmdReadSerial           = 0x89;

/// Hogsta matningstid enligt databladet ar 8.2 ms vid hog precision. Vi vantar
/// 10 ms, alltsa med marginal, men blockerar inte langre an sa. Att i stallet
/// polla bussen tills kretsen svarar hade varit snabbare men ger NACK-trafik som
/// stor andra enheter pa samma buss.
constexpr uint32_t kMeasureDelayMs = 10;
} // namespace

Sht4xDevice::Sht4xDevice(TwoWire& wire, uint8_t address, IClock& clock)
    : wire_(wire), address_(address), clock_(clock), last_ok_(false),
      last_error_(Reason::NotInitialised), temperature_c_(0.0f),
      humidity_percent_(0.0f), last_ms_(0) {}

uint8_t Sht4xDevice::crc8(const uint8_t* data, size_t len) {
    // Sensirions CRC8: polynom 0x31, startvarde 0xFF, ingen slutlig XOR.
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                               : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

bool Sht4xDevice::begin() {
    wire_.beginTransmission(address_);
    wire_.write(kCmdSoftReset);
    if (wire_.endTransmission() != 0) {
        last_error_ = Reason::NoResponse;
        return false;
    }
    delay(2); // databladet: mjuk aterstallning tar hogst 1 ms

    // Kontrollera att kretsen verkligen ar en SHT4x genom att lasa serienumret.
    // Enbart en ACK pa adressen racker inte, nastan vad som helst pa bussen kan
    // ge det.
    uint32_t serial = 0;
    if (!readSerialNumber(serial)) {
        last_error_ = Reason::NoResponse;
        return false;
    }
    last_error_ = Reason::None;
    return true;
}

bool Sht4xDevice::readSerialNumber(uint32_t& out) {
    wire_.beginTransmission(address_);
    wire_.write(kCmdReadSerial);
    if (wire_.endTransmission() != 0) return false;
    delay(2);

    if (wire_.requestFrom(address_, static_cast<uint8_t>(6)) != 6) return false;
    uint8_t buf[6];
    for (uint8_t i = 0; i < 6; ++i) buf[i] = static_cast<uint8_t>(wire_.read());

    if (crc8(&buf[0], 2) != buf[2] || crc8(&buf[3], 2) != buf[5]) return false;

    out = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
          (static_cast<uint32_t>(buf[3]) << 8) | static_cast<uint32_t>(buf[4]);
    return true;
}

bool Sht4xDevice::measure() {
    wire_.beginTransmission(address_);
    wire_.write(kCmdMeasureHighPrecision);
    if (wire_.endTransmission() != 0) {
        last_ok_ = false;
        last_error_ = Reason::NoResponse;
        return false;
    }

    delay(kMeasureDelayMs);

    if (wire_.requestFrom(address_, static_cast<uint8_t>(6)) != 6) {
        last_ok_ = false;
        last_error_ = Reason::NoResponse;
        return false;
    }

    uint8_t buf[6];
    for (uint8_t i = 0; i < 6; ++i) buf[i] = static_cast<uint8_t>(wire_.read());

    // Tva separata CRC, en per storhet. En trasig kabel slar oftast ut bara den
    // ena, och da vill vi behalla den andra.
    const bool t_ok = crc8(&buf[0], 2) == buf[2];
    const bool rh_ok = crc8(&buf[3], 2) == buf[5];
    if (!t_ok || !rh_ok) {
        last_ok_ = false;
        last_error_ = Reason::ChecksumFailed;
        return false;
    }

    const uint16_t t_raw = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
    const uint16_t rh_raw = static_cast<uint16_t>((buf[3] << 8) | buf[4]);

    // Omvandlingsformler ur databladet.
    temperature_c_ = -45.0f + 175.0f * (static_cast<float>(t_raw) / 65535.0f);
    float rh = -6.0f + 125.0f * (static_cast<float>(rh_raw) / 65535.0f);

    // Databladet anger att den rakonverterade fuktigheten kan hamna nagra
    // tiondelar utanfor 0 till 100 vid andarna, och att den ska klippas dar.
    // Klippningen goms inte: en fuktighet som klipps vid 100 ar fortfarande
    // korrekt, och en sensor som fastnat pa 100 fangas av stuck-kontrollen.
    if (rh < 0.0f) rh = 0.0f;
    if (rh > 100.0f) rh = 100.0f;
    humidity_percent_ = rh;

    last_ms_ = clock_.nowMs();
    last_ok_ = true;
    last_error_ = Reason::None;
    return true;
}

void Sht4xDevice::ensureFresh(uint32_t max_age_ms) {
    const uint32_t now = clock_.nowMs();
    if (last_ms_ == 0 || (now - last_ms_) > max_age_ms) {
        measure();
    }
}

RawReading Sht4xTemperature::read() {
    dev_.ensureFresh();
    if (!dev_.lastOk()) {
        return RawReading::fail(dev_.lastError(), dev_.lastMeasurementMs());
    }
    return RawReading::good(dev_.temperatureC(), dev_.lastMeasurementMs());
}

RawReading Sht4xHumidity::read() {
    dev_.ensureFresh();
    if (!dev_.lastOk()) {
        return RawReading::fail(dev_.lastError(), dev_.lastMeasurementMs());
    }
    return RawReading::good(dev_.humidityPercent(), dev_.lastMeasurementMs());
}

} // namespace hw
} // namespace mh

#endif // ARDUINO
