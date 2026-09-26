// MicroHydros - enhetskonfiguration
//
// Allt som skiljer en enhet fran en annan, eller en installation fran en annan,
// ligger har. Inget av det ar utspritt i koden.
//
// Inga riktiga losenord ska checkas in. Vardena nedan ar platshallare. I en
// skarp installation satts de antingen med -D vid bygget, till exempel
//
//     pio run -e esp32dev -D MH_WIFI_SSID='"odling"' -D MH_WIFI_PASS='"hemligt"'
//
// eller sa lases de ur NVS vid uppstart, vilket ar rekommendationen infor nasta
// utvecklingsfas. Se docs/teknisk-dokumentation.md, avsnittet om kanda
// begransningar.

#pragma once

// --- Identitet -------------------------------------------------------------

#ifndef MH_DEVICE_ID
#define MH_DEVICE_ID "microhydros-01"
#endif

#ifndef MH_FIRMWARE_VERSION
#define MH_FIRMWARE_VERSION "1.0.0"
#endif

// --- Natverk ---------------------------------------------------------------

#ifndef MH_WIFI_SSID
#define MH_WIFI_SSID "ANDRA-MIG"
#endif

#ifndef MH_WIFI_PASS
#define MH_WIFI_PASS "ANDRA-MIG"
#endif

#ifndef MH_MQTT_HOST
#define MH_MQTT_HOST "192.168.1.10"
#endif

#ifndef MH_MQTT_PORT
#define MH_MQTT_PORT 1883
#endif

// Satt till nullptr om brokern ar oppen. En oppen broker ar acceptabelt pa ett
// isolerat labbnat men inte i en skarp installation, se kanda begransningar.
#ifndef MH_MQTT_USER
#define MH_MQTT_USER nullptr
#endif

#ifndef MH_MQTT_PASS
#define MH_MQTT_PASS nullptr
#endif

// --- Amnen -----------------------------------------------------------------
//
// Versionsnumret ligger i sokvagen och inte bara i nyttolasten. Da kan en
// framtida schemaversion rullas ut parallellt med den gamla: nya enheter
// publicerar pa v2 medan gamla fortsatter pa v1, och mottagaren prenumererar pa
// bada under overgangen.

#define MH_TOPIC_TELEMETRY "microhydros/v1/" MH_DEVICE_ID "/telemetry"
#define MH_TOPIC_STATUS    "microhydros/v1/" MH_DEVICE_ID "/status"

// --- Anslutning av givare --------------------------------------------------
//
// ESP32 DevKitC. I2C pa standardpinnarna, 1-Wire pa en pinne utan specialfunktion
// vid uppstart.
//
//   GPIO21  SDA   till bada SHT4x, 4.7 kohm pullup mot 3V3
//   GPIO22  SCL   till bada SHT4x, 4.7 kohm pullup mot 3V3
//   GPIO4   DQ    till DS18B20, 4.7 kohm pullup mot 3V3
//
// GPIO4 ar valt medvetet. Flera av ESP32:ns pinnar har funktioner vid uppstart
// (GPIO0 bootlage, GPIO2 och GPIO15 likasa, GPIO12 styr flashspanningen och kan
// gora kortet ostartbart om den dras hog). GPIO4 ar fri fran allt sadant.

#define MH_PIN_SDA     21
#define MH_PIN_SCL     22
#define MH_PIN_ONEWIRE 4

// I2C-adresser. SHT45 och SHT40-AD1B ligger pa 0x44, SHT40-BD1B pa 0x45. Det ar
// darfor de bada luftgivarna kan sitta pa samma buss utan multiplexer.
#define MH_I2C_ADDR_SHT45_INSIDE  0x44
#define MH_I2C_ADDR_SHT40_OUTSIDE 0x45

// 100 kHz och inte 400 kHz. Givarna sitter i annan ande av en kabel i en fuktig
// miljo, och den lagre hastigheten ger betydligt battre marginal mot kapacitansen
// i kabeln. Vi laser fyra tal var tionde sekund, sa hastighet ar inget vi behover.
#define MH_I2C_FREQ_HZ 100000

// --- Tidtabell -------------------------------------------------------------
//
// Motivering finns i docs/adr/ADR-003-samplingsintervall.md.

#define MH_SAMPLE_INTERVAL_MS   10000UL   // las givarna var tionde sekund
#define MH_PUBLISH_INTERVAL_MS  60000UL   // skicka telemetri varje minut
#define MH_STATUS_INTERVAL_MS   300000UL  // livstecken var femte minut

// Gransen som forvarningen raknar tid kvar till. 30 C ar dar sallad och de
// flesta bladortar borjar ta skada i ett slutet skap.
#define MH_INSIDE_HIGH_LIMIT_C 30.0f

// --- Watchdog --------------------------------------------------------------
//
// 30 sekunder ar valt sa att det ar langre an det langsta vi rimligen kan fastna
// i (en MQTT-anslutning med 5 sekunders sockettimeout) men kortare an en
// publiceringsperiod, sa att en hangd enhet startar om innan nagon hinner sakna
// den.
#define MH_WATCHDOG_TIMEOUT_S 30
