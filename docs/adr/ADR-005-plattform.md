# ADR-005: Hårdvaruplattform och kodstruktur

**Status:** Beslutad
**Datum:** vecka 1
**Berör:** NFR-3, NFR-5

## Sammanhang

Prototypen behöver en mikrokontroller som klarar I²C, 1-Wire och nätverk, och en
kodstruktur som gör att lösningen går att testa och att lämna över.

Det andra kravet är minst lika viktigt som det första. Kunden skriver att de vill
förstå hur vi resonerat och hur arbetet kan fortsätta i nästa fas, och
projektbeskrivningen tar upp att en annan utvecklare ska kunna förstå systemets
uppbyggnad.

## Val av mikrokontroller

| Kort | Nät | RAM | Pris | Kommentar |
|---|---|---|---|---|
| Arduino Uno R3 | nej | 2 kB | 250 kr | för lite minne, kräver shield för nät |
| **ESP32-WROOM-32** | **WiFi + BLE** | **520 kB** | **80 kr** | valt |
| ESP32-C3 | WiFi + BLE | 400 kB | 60 kr | fungerar lika bra, färre pinnar |
| Raspberry Pi Pico W | WiFi | 264 kB | 90 kr | bra kort, svagare ekosystem för I²C-givare |
| Raspberry Pi Zero 2 W | WiFi | 512 MB | 250 kr | Linux, överdimensionerat och kräver ordnad avstängning |

**Beslut: ESP32-WROOM-32 på ett DevKitC-kort.**

Skälen, i fallande viktordning:

**WiFi inbyggt.** Kunden vill koppla flera enheter till en gemensam tjänst.
Enheter som står i bostäder och restauranger ska prata över husets wifi, inte
över en kabel.

**Gott om minne.** 520 kB RAM betyder att vi kan allokera hela telemetrikön
statiskt, 32 kB, utan att tänka på det. På en Uno hade kön fått plats fyra
gånger om i hela minnet.

**Inget filsystem att korrumpera.** En Raspberry Pi med Linux hade varit bekvämt
men kräver ordnad avstängning. En odlingsenhet i ett hem kommer att förlora
strömmen, och ett SD-kort som tappar strömmen mitt i en skrivning går sönder på
riktigt. En mikrokontroller startar bara om.

**Mogen verktygskedja.** PlatformIO med Arduino-ramverket på ESP32 är väl
beprövat, och viktigast av allt: det finns en `native`-plattform som gör att
samma kod kan kompileras och testas på utvecklingsdatorn.

Vi övervägde ESP-IDF direkt istället för Arduino-ramverket. Det hade gett riktiga
FreeRTOS-tasks, NVS för konfiguration och en MQTT-klient med QoS 1. Vi valde bort
det för en första prototyp eftersom uppstartströskeln är högre och vinsten inte
syns förrän man behöver just de sakerna. Det ligger som rekommendation inför
nästa fas, och bytet berör bara `mh_hw`.

## Val av kodstruktur

Det här beslutet betyder mer för projektets resultat än valet av kort.

### Tre bibliotek med en skarp gräns

```
mh_core    ren C++17, inga hårdvaruberoenden alls
mh_sim     termisk modell och simulerade givare, portabel
mh_hw      allt som rör ESP32, helt inuti #ifdef ARDUINO
```

`mh_core` känner till exakt fyra gränssnitt mot omvärlden: `ISensor`, `IClock`,
`ITransport` och `ILogger`. Ingenting annat.

### Tre byggmiljöer som faller ut av strukturen

| Miljö | Vad som byggs | Vad den används till |
|---|---|---|
| `esp32dev` | mh_core + mh_hw | firmware till kortet |
| `native` | mh_core + mh_sim | 82 enhetstester på utvecklingsdatorn |
| `simnode` | mh_core + mh_sim | applikationen mot simulerad hårdvara |

## Varför det här var rätt beslut

Det går att mäta.

**Testcykeln.** Hela testsviten, 82 tester inklusive sju simulerade dygn, tar
under sex sekunder. Samma tester på hårdvara hade krävt flashning, seriell
avläsning och sju verkliga dygn. Med snabb återkoppling blir det naturligt att
köra testerna efter varje ändring.

**Felen kommer när vi vill.** Att provocera fram en DS18B20 som rapporterar
85 °C på riktig hårdvara innebär att dra ur en kabel vid rätt tillfälle och hoppas
att det inträffar under mätningen. I simuleringen är det ett funktionsanrop, det
sker exakt när vi bestämmer, och det går att köra om identiskt.

**Tre verkliga fel hittades utan hårdvara.** Buffertbudgeten som var för liten,
falsklarmet vid lampstart och falsklarmet från sensorbrus. Alla tre hade varit
mycket svårare att hitta genom att titta i en seriell terminal, och det andra och
tredje hade sannolikt inte upptäckts alls förrän enheten stått ute i veckor.

**Det som testas är det som körs.** Det här är kärnan. Hade vi skrivit en separat
testmodell av logiken hade vi testat modellen och inte firmwaren. Nu är
`MicroHydrosApp` bokstavligen samma objektkod i alla tre miljöerna, bara länkad
mot olika drivrutiner.

## Vad det kostar

Abstraktionen är inte gratis.

**Ett extra lager att läsa sig igenom.** Den som vill se hur en SHT45 läses måste
gå via `ISensor` till `Sht4xSensor`. I en liten prototyp känns det omständligt
första gången.

**Virtuella anrop.** Varje sensoravläsning går via en vtable. Vid tio sekunders
intervall och fyra kanaler är det 24 indirekta anrop i minuten, alltså ingenting
mätbart. På en enhet som skulle sampla i kilohertz hade det varit en fråga.

**Disciplin krävs.** Det är lätt att i ett svagt ögonblick inkludera `Arduino.h`
i `mh_core` för att snabbt få tag på `millis()`. Då slutar native-bygget
fungera, och frestelsen finns att då bara sluta köra native-testerna.
`lib_ignore` i varje miljö gör att det bryter direkt istället för att degradera
tyst.

## Anslutning

```
ESP32 DevKitC

GPIO21  SDA  ──┬── SHT45   (inne,  I²C 0x44)
               └── SHT40   (ute,   I²C 0x45)     4,7 kΩ pullup mot 3V3
GPIO22  SCL  ──┴──                                4,7 kΩ pullup mot 3V3

GPIO4   DQ   ───── DS18B20 (vatten, 1-Wire)       4,7 kΩ pullup mot 3V3
```

GPIO4 är valt medvetet. Flera av ESP32:ns pinnar har funktioner vid uppstart:
GPIO0 styr bootläget, GPIO2 och GPIO15 likaså, och GPIO12 bestämmer
flashspänningen och kan göra kortet ostartbart om den dras hög vid reset. GPIO4
är fri från allt sådant.

I²C-hastigheten är satt till 100 kHz och inte 400 kHz. Givarna sitter i andra
änden av en kabel i en fuktig miljö, och den lägre hastigheten ger betydligt
bättre marginal mot kabelkapacitansen. Vi läser fyra tal var tionde sekund, så
hastighet är inget vi behöver.

## Minnesbudget

Uppmätt vid bygge, inte uppskattat:

```
RAM   79 148 byte av 327 680   24,2 %
Flash 783 785 byte av 1 310 720  59,8 %
```

Av RAM-användningen är 32 kB telemetrikön, 16 ramar à 2 kB. Resten är WiFi- och
MQTT-stackarna. Det finns alltså gott om utrymme för att växa, vilket är skälet
till att kön kunde göras statisk utan att snåla.

## Konsekvenser

- Logiken går att testa utan hårdvara, och det som testas är det som körs.
- Ett byte av mikrokontroller eller givare berör bara `mh_hw`.
- Ett byte till ESP-IDF, som behövs för QoS 1, berör också bara `mh_hw`.
- Strukturen kräver att man håller isär lagren, och verktygskedjan är
  konfigurerad för att bryta högt om man inte gör det.
