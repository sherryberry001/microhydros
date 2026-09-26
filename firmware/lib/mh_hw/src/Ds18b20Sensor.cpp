#ifdef ARDUINO

#include "mh/hw/Ds18b20Sensor.h"

namespace mh {
namespace hw {

namespace {
constexpr uint8_t kFamilyDs18b20   = 0x28;
constexpr uint8_t kCmdConvertT     = 0x44;
constexpr uint8_t kCmdReadScratch  = 0xBE;
constexpr uint8_t kCmdWriteScratch = 0x4E;
constexpr uint8_t kCmdSkipRom      = 0xCC;
constexpr uint8_t kCmdMatchRom     = 0x55;

/// 12 bitars upplosning tar hogst 750 ms enligt databladet. Vi laser tidigast
/// 800 ms efter start for att ha marginal mot kretsens interna oscillator, som
/// har en tolerans pa nagra procent.
constexpr uint32_t kConversionMs = 800;
} // namespace

Ds18b20Sensor::Ds18b20Sensor(OneWire& bus, IClock& clock, const char* id)
    : bus_(bus), clock_(clock), id_(id), rom_{0}, found_(false),
      conversion_pending_(false), conversion_started_ms_(0) {}

bool Ds18b20Sensor::begin() {
    found_ = false;
    conversion_pending_ = false;

    bus_.reset_search();
    uint8_t addr[8];
    while (bus_.search(addr)) {
        // Familjekoden ar forsta byten. Vi vill bara ha DS18B20, inte en
        // DS18S20 eller en DS2405 som rakar sitta pa samma buss.
        if (addr[0] != kFamilyDs18b20) continue;
        // ROM-koden har sin egen CRC8. En sond med dalig kontakt hittas ofta men
        // med trasig adress, och da skulle alla efterfoljande matningar ga till
        // ingenstans.
        if (OneWire::crc8(addr, 7) != addr[7]) continue;

        for (uint8_t i = 0; i < 8; ++i) rom_[i] = addr[i];
        found_ = true;
        break;
    }

    if (!found_) return false;
    if (!setResolution12Bit()) return false;

    // Starta en forsta konvertering direkt, sa att det finns ett varde att hamta
    // vid den forsta ordinarie avlasningen.
    return startConversion();
}

bool Ds18b20Sensor::setResolution12Bit() {
    if (!bus_.reset()) return false;
    bus_.select(rom_);
    bus_.write(kCmdWriteScratch);
    bus_.write(0x4B); // TH, larmgrans hog, anvands inte men maste skrivas
    bus_.write(0x46); // TL, larmgrans lag
    bus_.write(0x7F); // konfiguration: 0x7F = 12 bitar, 0.0625 C upplosning
    return true;
}

bool Ds18b20Sensor::startConversion() {
    if (!found_) return false;
    if (!bus_.reset()) return false;
    bus_.select(rom_);
    // Ingen parasitmatning: vi driver sonden med tre ledare och 4.7 kohm pullup.
    // Med parasitmatning hade bussen behovt hallas hog under hela konverteringen
    // och drivrutinen hade tvingats blockera, vilket ar hela det vi undviker har.
    bus_.write(kCmdConvertT, 0);
    conversion_started_ms_ = clock_.nowMs();
    conversion_pending_ = true;
    return true;
}

bool Ds18b20Sensor::readScratchpad(int16_t& raw_out) {
    if (!bus_.reset()) return false;
    bus_.select(rom_);
    bus_.write(kCmdReadScratch);

    uint8_t data[9];
    for (uint8_t i = 0; i < 9; ++i) data[i] = bus_.read();

    // Nionde byten ar CRC8 over de atta forsta. Utan den kontrollen skulle en
    // storning pa bussen bli ett matvarde som ser helt normalt ut.
    if (OneWire::crc8(data, 8) != data[8]) return false;

    raw_out = static_cast<int16_t>((static_cast<uint16_t>(data[1]) << 8) | data[0]);
    return true;
}

RawReading Ds18b20Sensor::read() {
    const uint32_t now = clock_.nowMs();

    if (!found_) {
        return RawReading::fail(Reason::NoResponse, now);
    }

    if (!conversion_pending_) {
        startConversion();
        return RawReading::fail(Reason::Stale, now);
    }

    // Osignerad subtraktion, korrekt aven nar millis() slar runt.
    if ((now - conversion_started_ms_) < kConversionMs) {
        // Konverteringen ar inte klar an. Det hander bara om nagon anropar
        // oftare an kConversionMs, alltsa inte i normal drift dar intervallet
        // ar tio sekunder.
        return RawReading::fail(Reason::Stale, now);
    }

    int16_t raw = 0;
    if (!readScratchpad(raw)) {
        conversion_pending_ = false;
        startConversion();
        return RawReading::fail(Reason::ChecksumFailed, now);
    }

    const float celsius = static_cast<float>(raw) * 0.0625f;

    // Tidsstampeln ar nar den har konverteringen startade, inte nar vi hamtade
    // resultatet. Annars skulle derivatakontrollen rakna pa fel tidsbas. Den
    // maste sparas undan innan nasta konvertering startas, eftersom den skriver
    // over faltet.
    const uint32_t sampled_at_ms = conversion_started_ms_;

    // Starta nasta konvertering med en gang, sa att vardet ar fardigt till nasta
    // avlasning om tio sekunder.
    conversion_pending_ = false;
    startConversion();

    return RawReading::good(celsius, sampled_at_ms);
}

} // namespace hw
} // namespace mh

#endif // ARDUINO
