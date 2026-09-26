// MicroHydros - simuleringsnod
//
// Kor exakt samma MicroHydrosApp som hardvaran, men lankad mot GrowBoxModel och
// simulerade givare istallet for I2C och 1-Wire, och med en klocka som kan gå
// fortare an realtid. Ramarna skrivs pa stdout i samma radformat som enheten
// anvander pa sin UART, sa gatewayens serial-brygga tar emot dem utan att veta
// att det inte ar ett kort i andra anden.
//
// Anvandning:
//   ./simnode --scenario normal --hours 24 --speed 600
//
// Scenarier:
//   normal     Ostord drift over den valda tiden.
//   faults     Fyra injicerade fel i tur och ordning, ett per kvart.
//   outage     Natet forsvinner mitt i och kommer tillbaka.
//   heatwave   Kylningen slutar fungera, for att visa forvarningen.
//
// All slump har fast fro, sa tva korningar med samma argument ger identiska
// siffror. Det ar avsiktligt: testresultat som inte gar att reproducera ar inte
// testresultat.

#ifndef ARDUINO

#include "mh/MicroHydrosApp.h"
#include "mh/LineTransport.h"
#include "mh/sim/GrowBoxModel.h"
#include "mh/sim/SimSensors.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

using namespace mh;
using namespace mh::sim;

namespace {

class StdoutSink : public ILineSink {
public:
    bool write(const char* data, size_t len) override {
        if (!online_) return false;
        std::fwrite(data, 1, len, stdout);
        return true;
    }
    bool endLine() override {
        if (!online_) return false;
        std::fputc('\n', stdout);
        std::fflush(stdout); // bryggan ska se ramen direkt, inte nar bufferten fylls
        ++written_;
        return true;
    }
    bool ready() const override { return online_; }

    void setOnline(bool v) { online_ = v; }
    unsigned long written() const { return written_; }

private:
    bool          online_ = true;
    unsigned long written_ = 0;
};

class StderrLogger : public ILogger {
public:
    void log(const char* line) override { std::fprintf(stderr, "[fw] %s\n", line); }
};

struct Options {
    std::string scenario = "normal";
    std::string device;   ///< tomt = harleds ur scenariot
    double      hours = 1.0;
    double      speed = 600.0; ///< simulerade sekunder per verklig sekund
    bool        realtime = false;
    uint32_t    seed = 0xC0FFEEu;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has_next = (i + 1) < argc;
        if (a == "--scenario" && has_next)      o.scenario = argv[++i];
        else if (a == "--device" && has_next)   o.device = argv[++i];
        else if (a == "--hours" && has_next)    o.hours = std::atof(argv[++i]);
        else if (a == "--speed" && has_next)    o.speed = std::atof(argv[++i]);
        else if (a == "--seed" && has_next)     o.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        else if (a == "--realtime")             o.realtime = true;
        else if (a == "--help") {
            std::fprintf(stderr,
                "simnode --scenario normal|faults|outage|heatwave "
                "--hours N --speed N [--seed N] [--device ID]\n");
            std::exit(0);
        }
    }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);

    GrowBoxModel model;
    SimClock     clock;
    Rng          rng(opt.seed);
    StdoutSink   sink;
    LineTransport transport(sink);
    StderrLogger logger;

    SimSht45Temperature air_in(model, clock, rng);
    SimSht40Temperature air_out(model, clock, rng);
    SimDs18b20          water(model, clock, rng);
    SimSht45Humidity    rh_in(model, clock, rng);

    // Varje scenario far ett eget enhets-id om inget anges. Da kan alla fyra
    // koras mot samma gateway och ligga kvar i samma databas utan att krocka,
    // vilket ocksa ar ett litet prov pa att mottagaren klarar flera enheter. Det
    // ar riktningen kunden beskrev: flera odlingsenheter mot en gemensam tjanst.
    const std::string device_id =
        opt.device.empty() ? ("microhydros-sim-" + opt.scenario) : opt.device;
    const std::string topic_telemetry = "microhydros/v1/" + device_id + "/telemetry";
    const std::string topic_status = "microhydros/v1/" + device_id + "/status";

    AppConfig cfg;
    cfg.device_id = device_id.c_str();
    cfg.topic_telemetry = topic_telemetry.c_str();
    cfg.topic_status = topic_status.c_str();

    MicroHydrosApp app(cfg, clock, transport, &logger);
    app.attachSensor(kChAirInside, &air_in);
    app.attachSensor(kChAirOutside, &air_out);
    app.attachSensor(kChWater, &water);
    app.attachSensor(kChHumidityInside, &rh_in);

    if (!app.begin()) {
        std::fprintf(stderr, "[fw] varning: minst en sensor startade inte\n");
    }

    const long total_s = static_cast<long>(opt.hours * 3600.0);
    std::fprintf(stderr, "[sim] scenario=%s, simulerar %ld sekunder, fro=0x%X\n",
                 opt.scenario.c_str(), total_s, opt.seed);

    // Scenarierna ar tidsstyrda sa att de gar att kora om exakt likadant och sa
    // att demonstrationen blir densamma varje gang.
    const long q = total_s / 5;

    for (long t = 0; t < total_s; ++t) {
        if (opt.scenario == "faults") {
            if (t == q * 1)     { water.setFault(FaultMode::Ds18b20PowerOnValue); std::fprintf(stderr, "[sim] t=%ld vattensonden tappar kontakt, rapporterar 85 C\n", t); }
            if (t == q * 1 + 300) { water.clearFault(); std::fprintf(stderr, "[sim] t=%ld vattensonden ater ansluten\n", t); }
            if (t == q * 2)     { rh_in.setFault(FaultMode::CrcFailure); std::fprintf(stderr, "[sim] t=%ld fuktkanalen ger CRC-fel\n", t); }
            if (t == q * 2 + 300) { rh_in.clearFault(); std::fprintf(stderr, "[sim] t=%ld fuktkanalen frisk igen\n", t); }
            if (t == q * 3)     { air_in.setFault(FaultMode::StuckValue); std::fprintf(stderr, "[sim] t=%ld inneluften fastnar pa samma varde\n", t); }
            if (t == q * 3 + 600) { air_in.clearFault(); std::fprintf(stderr, "[sim] t=%ld inneluften frisk igen\n", t); }
            if (t == q * 4)     { air_out.setFault(FaultMode::NoResponse); std::fprintf(stderr, "[sim] t=%ld utegivaren slutar svara\n", t); }
            if (t == q * 4 + 300) { air_out.clearFault(); std::fprintf(stderr, "[sim] t=%ld utegivaren svarar igen\n", t); }
        } else if (opt.scenario == "outage") {
            if (t == q * 2) { sink.setOnline(false); std::fprintf(stderr, "[sim] t=%ld natet forsvinner\n", t); }
            if (t == q * 4) { sink.setOnline(true); std::fprintf(stderr, "[sim] t=%ld natet ar tillbaka, kön ska tommas\n", t); }
        } else if (opt.scenario == "heatwave") {
            // Borja i morkerperioden sa att skapet ar svalt och klimatet har
            // hunnit sta still. Da blir forloppet efterat representativt: en
            // odling som star i jamvikt och dar nagot gar sonder.
            if (t == 0) {
                model.forceLamp(false);
                std::fprintf(stderr, "[sim] morkerperiod, skapet far svalna\n");
            }
            if (t == q * 2) {
                // Lampan tander och samtidigt har kylningen slutat fungera.
                // Jamvikten hamnar nu pa ca 33 C istallet for 27.5, alltsa over
                // gransen, och skapet ar drygt tio minuter fran att na dit.
                model.forceLamp(true);
                model.injectHeatStep(6.0f);
                std::fprintf(stderr, "[sim] t=%ld lampan tander men kylningen ar ur funktion\n", t);
            }
        }

        model.step(1.0f);
        clock.advance(1000);
        app.loop();
    }

    const AppStats& s = app.stats();
    std::fprintf(stderr,
                 "\n[sim] klart\n"
                 "  avlasningar : %u (good %u, suspect %u, bad %u)\n"
                 "  ramar byggda: %u\n"
                 "  ramar sanda : %u\n"
                 "  ramar i kö  : %u\n"
                 "  ramar tappade: %u\n"
                 "  bussomstarter: %u\n",
                 s.samples_taken, s.samples_good, s.samples_suspect, s.samples_bad,
                 s.frames_built, s.frames_sent,
                 static_cast<unsigned>(app.queuedFrames()), s.frames_dropped,
                 s.bus_reinits);

    return 0;
}

#endif // !ARDUINO
