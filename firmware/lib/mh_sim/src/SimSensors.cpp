#include "mh/sim/SimSensors.h"

#include <cmath>

namespace mh {
namespace sim {

SimSensorBase::SimSensorBase(const char* id, Quantity q, Location loc,
                             const GrowBoxModel& model, IClock& clock, Rng& rng)
    : id_(id), quantity_(q), location_(loc), model_(model), clock_(clock), rng_(rng),
      fault_(FaultMode::None), fault_probability_(1.0f), drift_per_hour_(0.0f),
      accumulated_drift_(0.0f), last_value_(0.0f), has_last_(false), last_ms_(0),
      started_(false) {}

bool SimSensorBase::begin() {
    // En omstart av bussen nollstaller en hangd krets. Det aterspeglas har: efter
    // begin() ar kretsen frisk igen om felet var ett busstillstand, men inte om
    // den ar fysiskt trasig. Permanenta fel (probability 1.0) overlever, vilket
    // ar just det beteende som gor att applikationens omstartslogik gar att testa.
    started_ = true;
    if (fault_probability_ < 1.0f) {
        has_last_ = false;
    }
    return true;
}

RawReading SimSensorBase::readWithFaults() {
    const uint32_t now = clock_.nowMs();

    if (!started_) {
        return RawReading::fail(Reason::NotInitialised, now);
    }

    const bool fault_active =
        fault_ != FaultMode::None && rng_.uniform() < fault_probability_;

    if (fault_active) {
        switch (fault_) {
            case FaultMode::CrcFailure:
                return RawReading::fail(Reason::ChecksumFailed, now);
            case FaultMode::NoResponse:
                return RawReading::fail(Reason::NoResponse, now);
            case FaultMode::StuckValue:
                if (has_last_) {
                    return RawReading::good(last_value_, now);
                }
                break; // inget varde att frysa an, ge ett riktigt
            case FaultMode::Ds18b20PowerOnValue:
                // Registervardet efter reset. Ser ut som en helt normal matning.
                last_value_ = 85.0f;
                has_last_ = true;
                return RawReading::good(85.0f, now);
            case FaultMode::Spike: {
                // Enstaka utslag, sadana som kommer av en storning pa bussen eller
                // en dalig lodning. Riktningen slumpas.
                const float magnitude = 20.0f + rng_.uniform() * 30.0f;
                const float sign = rng_.uniform() < 0.5f ? -1.0f : 1.0f;
                const float v = trueValue() + sign * magnitude;
                return RawReading::good(v, now);
            }
            case FaultMode::Drift:
            case FaultMode::None:
                break;
        }
    }

    // Normal avlasning: sant varde, drift, brus, kvantisering.
    float v = trueValue();

    if (drift_per_hour_ != 0.0f) {
        if (last_ms_ != 0 && now > last_ms_) {
            const float dt_h = static_cast<float>(now - last_ms_) / 3600000.0f;
            accumulated_drift_ += drift_per_hour_ * dt_h;
        }
        v += accumulated_drift_;
    }

    v += rng_.gaussian() * noiseSigma();

    const float step = resolution();
    if (step > 0.0f) {
        v = std::round(v / step) * step;
    }

    last_value_ = v;
    has_last_ = true;
    last_ms_ = now;
    return RawReading::good(v, now);
}

} // namespace sim
} // namespace mh
