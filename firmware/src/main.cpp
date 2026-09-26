// MicroHydros - firmware for ESP32
//
// Den har filen ar avsiktligt kort. Allt beteende ligger i MicroHydrosApp, som
// inte kanner till nagon hardvara alls. Har kopplas bara ihop de riktiga
// drivrutinerna med applikationen, precis som simnode_main.cpp kopplar ihop de
// simulerade.
//
// Att de tva ar sa lika ar hela poangen: det som kors i simuleringen och i
// enhetstesterna ar samma kod som kors pa kortet.
//
// Kopplingsschema och komponentlista finns i docs/teknisk-dokumentation.md.

#ifdef ARDUINO

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <esp_task_wdt.h>

#include "config.h"

#include "mh/MicroHydrosApp.h"
#include "mh/LineTransport.h"
#include "mh/hw/Platform.h"
#include "mh/hw/Sht4xSensor.h"
#include "mh/hw/Ds18b20Sensor.h"
#include "mh/hw/MqttTransport.h"

using namespace mh;
using namespace mh::hw;

namespace {

ArduinoClock g_clock;
SerialLogger g_logger;

OneWire g_onewire(MH_PIN_ONEWIRE);

// Tva SHT4x pa samma buss, atskilda av adressen. Varje krets ager sin egen
// I2C-konversation, och de tva kanalvyerna delar pa matningen.
Sht4xDevice g_sht_inside(Wire, MH_I2C_ADDR_SHT45_INSIDE, g_clock);
Sht4xDevice g_sht_outside(Wire, MH_I2C_ADDR_SHT40_OUTSIDE, g_clock);

Sht4xTemperature g_air_in(g_sht_inside, "air_in", Location::Inside);
Sht4xHumidity    g_rh_in(g_sht_inside, "rh_in", Location::Inside);
Sht4xTemperature g_air_out(g_sht_outside, "air_out", Location::Outside);
Ds18b20Sensor    g_water(g_onewire, g_clock, "water");

// Last Will. Brokern lagger ut den har om enheten forsvinner utan att saga hejda.
const char kLwtPayload[] =
    "{\"schema_version\":1,\"device_id\":\"" MH_DEVICE_ID
    "\",\"state\":\"offline\",\"firmware\":\"" MH_FIRMWARE_VERSION "\"}";

NetConfig g_net = {
    MH_WIFI_SSID, MH_WIFI_PASS,
    MH_MQTT_HOST, MH_MQTT_PORT,
    MH_MQTT_USER, MH_MQTT_PASS,
    MH_DEVICE_ID,
    MH_TOPIC_STATUS,
    kLwtPayload,
};

MqttTransport g_mqtt(g_net, g_clock, &g_logger);

// Seriell spegel. Allt som gar ut pa MQTT skrivs ocksa pa USB-seriellen, sa att
// enheten gar att folja med en terminal utan att rora natverket, och sa att
// gatewayens serial-brygga kan anvandas som reservvag.
SerialSink    g_serial_sink;
LineTransport g_serial_transport(g_serial_sink);

// MQTT ar den primara vagen och avgor om en ram raknas som levererad. Spegeln far
// aldrig fa en ram att se levererad ut nar natet ar nere, for da slutar
// store-and-forward fungera.
TeeTransport g_transport(g_mqtt, g_serial_transport);

AppConfig makeConfig() {
    AppConfig c;
    c.device_id = MH_DEVICE_ID;
    c.firmware_version = MH_FIRMWARE_VERSION;
    c.topic_telemetry = MH_TOPIC_TELEMETRY;
    c.topic_status = MH_TOPIC_STATUS;
    c.sample_interval_ms = MH_SAMPLE_INTERVAL_MS;
    c.publish_interval_ms = MH_PUBLISH_INTERVAL_MS;
    c.status_interval_ms = MH_STATUS_INTERVAL_MS;
    c.inside_high_limit_c = MH_INSIDE_HIGH_LIMIT_C;
    return c;
}

MicroHydrosApp g_app(makeConfig(), g_clock, g_transport, &g_logger);

void logSensorIdentity() {
    uint32_t serial = 0;
    if (g_sht_inside.readSerialNumber(serial)) {
        Serial.printf("[mh] SHT45 inne, serienummer 0x%08X\n", serial);
    } else {
        Serial.println(F("[mh] SHT45 inne svarar inte"));
    }
    if (g_sht_outside.readSerialNumber(serial)) {
        Serial.printf("[mh] SHT40 ute, serienummer 0x%08X\n", serial);
    } else {
        Serial.println(F("[mh] SHT40 ute svarar inte"));
    }
    if (g_water.found()) {
        const uint8_t* rom = g_water.romCode();
        Serial.printf("[mh] DS18B20 ROM %02X%02X%02X%02X%02X%02X%02X%02X\n",
                      rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
    } else {
        Serial.println(F("[mh] DS18B20 hittades inte pa 1-Wire-bussen"));
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[mh] MicroHydros " MH_FIRMWARE_VERSION " startar"));

    // Vacka bussen innan Wire tar over. Om kortet startades om mitt i en
    // I2C-lasning hanger slaven kvar och haller SDA lag, och da hjalper det inte
    // att starta om mikrokontrollern.
    recoverI2cBus(MH_PIN_SDA, MH_PIN_SCL);

    Wire.begin(MH_PIN_SDA, MH_PIN_SCL, MH_I2C_FREQ_HZ);

    g_app.attachSensor(kChAirInside, &g_air_in);
    g_app.attachSensor(kChAirOutside, &g_air_out);
    g_app.attachSensor(kChWater, &g_water);
    g_app.attachSensor(kChHumidityInside, &g_rh_in);

    if (!g_app.begin()) {
        // Medvetet ingen omstart har. En enhet dar en av fyra givare saknas ska
        // fortsatta rapportera de tre som fungerar, med den fjarde markt som
        // felaktig, istallet for att starta om i en oandlig slinga. Ett halvt
        // fungerande system som beratter vad som ar trasigt ar mycket mer vart
        // for kunden an ett som bara blinkar.
        Serial.println(F("[mh] varning: minst en givare startade inte"));
    }

    logSensorIdentity();

    // Watchdogen startas sist, nar all initiering ar klar. Skulle applikationen
    // fastna i loopen startar kortet om av sig sjalvt.
    esp_task_wdt_init(MH_WATCHDOG_TIMEOUT_S, true);
    esp_task_wdt_add(nullptr);

    Serial.println(F("[mh] uppstart klar"));
}

void loop() {
    g_app.loop();
    esp_task_wdt_reset();

    // Kort vila. Loopen ar helt icke-blockerande, sa utan den skulle den snurra
    // sa fort den kan och bara ata strom. 20 ms ar kort nog for att MQTT:s
    // halsokontroller ska hinna svaras i tid.
    delay(20);
}

#endif // ARDUINO
