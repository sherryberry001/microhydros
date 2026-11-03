# Inlämning, Projekt 1: MicroHydros

**Kurs:** IoT- och embeddedutvecklare, 20 YH-poäng
**Projektperiod:** 31 augusti till 25 september 2026
**Studerande:** Shehryar Hassan
**Inlämningsdatum:** 26 september 2026

## Repository

**https://github.com/sherryberry001/microhydros**

Publikt repo. All kod, all dokumentation och alla bevisfiler ligger där.
Versionshistoriken består av 21 commits som följer projektets utveckling från
byggkonfiguration till färdig dokumentation.

```bash
git clone https://github.com/sherryberry001/microhydros.git
```

## Om arbetsformen

Projektet är genomfört individuellt. Jag arbetar heltid vid sidan av studierna
och har genomfört tidigare kurser på samma sätt. Det betyder att det inte finns
någon arbetsfördelning att redovisa, och att samtliga tekniska beslut, all kod
och all dokumentation i repot är mina egna. Samtliga delar av
utvecklingsprocessen, från kravanalys till testning och dokumentation, är
utförda av mig.

## Det här är den viktigaste begränsningen

Prototypen är verifierad i simulering, inte mot fysiska givare. Firmwaren
kompilerar för ESP32 och drivrutinerna är skrivna mot databladen för SHT4x och
DS18B20, men inget kort har varit inkopplat. All verifiering har skett mot en
fysikalisk modell av odlingsskåpet med injicerade fel, plus en riktig
MQTT-broker och riktig lagring i SQLite.

Begränsningen är beskriven i detalj i
[teknisk dokumentation, avsnitt 10.1](https://github.com/sherryberry001/microhydros/blob/main/docs/teknisk-dokumentation.md#101-prototypen-är-verifierad-i-simulering-inte-på-fysisk-hårdvara),
där det också står vad verifieringen bevisar och vad den inte bevisar.

---

## Leverabler enligt uppgiftens avsnitt 15

### 1. Projektplan och krav

| Dokument | Innehåll |
|---|---|
| [docs/projektplan.md](https://github.com/sherryberry001/microhydros/blob/main/docs/projektplan.md) | Sammanfattning av uppdraget, mål, avgränsningar, prioriterad backlog, tidsplan vecka för vecka, projektrisker med åtgärder |
| [docs/krav.md](https://github.com/sherryberry001/microhydros/blob/main/docs/krav.md) | 20 numrerade krav (FR-1 till FR-12, NFR-1 till NFR-8) med MoSCoW-prioritet, källa, var kravet är löst i koden, och vilka tester som verifierar det |

Kraven är spårbara åt båda hållen: varje krav pekar ut sin implementation och
sina testfall, och testrapporten pekar tillbaka på kraven.

### 2. Komplett Git-repository

**https://github.com/sherryberry001/microhydros**

21 commits. Varje commit är ett avgränsat steg med ett meddelande som förklarar
vad som tillkom och varför. Historiken går att läsa uppifrån och ner som
projektets förlopp.

```bash
git log --oneline
```

### 3. Fungerande prototyp

| Del | Var |
|---|---|
| Firmware för ESP32 | [firmware/](https://github.com/sherryberry001/microhydros/tree/main/firmware) |
| Applikationskärna utan hårdvaruberoenden | [firmware/lib/mh_core/](https://github.com/sherryberry001/microhydros/tree/main/firmware/lib/mh_core) |
| Drivrutiner för målhårdvaran | [firmware/lib/mh_hw/](https://github.com/sherryberry001/microhydros/tree/main/firmware/lib/mh_hw) |
| Simuleringsmodell och simulerade givare | [firmware/lib/mh_sim/](https://github.com/sherryberry001/microhydros/tree/main/firmware/lib/mh_sim) |
| Gateway: broker, mottagare, lagring, panel | [gateway/](https://github.com/sherryberry001/microhydros/tree/main/gateway) |

**Mätpunkter.** Fyra obligatoriska plus två härledda:

| Nr | Mätpunkt | Givare | Gränssnitt |
|---|---|---|---|
| 1 | Lufttemperatur inne | Sensirion SHT45 | I²C, 0x44 |
| 2 | Lufttemperatur ute | Sensirion SHT40-BD1B | I²C, 0x45 |
| 3 | Vattentemperatur | Maxim DS18B20, vattentät sond | 1-Wire |
| 4 | Relativ luftfuktighet inne | Sensirion SHT45 | I²C, 0x44 |
| 5 | Daggpunkt (härledd) | räknas ombord | Magnus-formeln |
| 6 | Ångtrycksdeficit, VPD (härledd) | räknas ombord | Magnus-formeln |

DHT11 och DHT22 används inte. Motiveringen för de valda givarna finns i
[ADR-001](https://github.com/sherryberry001/microhydros/blob/main/docs/adr/ADR-001-sensorval.md)
med jämförelsetabell mot DHT22, BME280, SHT31, AHT20 och HDC3022.

**Verifierat byggresultat:**

```
Enhetstester (native):  82 av 82 godkända
ESP32-bygge:            RAM 24,2 % (79 148 av 327 680 byte)
                        Flash 59,8 % (783 785 av 1 310 720 byte)
```

### 4. Teknisk dokumentation

| Dokument | Rader | Innehåll |
|---|---|---|
| [docs/teknisk-dokumentation.md](https://github.com/sherryberry001/microhydros/blob/main/docs/teknisk-dokumentation.md) | 748 | Arkitektur med diagram, hårdvaruval, kopplingsschema, komponentlista, sensorval, kommunikationslösning, programstruktur, datamodell, kända begränsningar, rekommendationer |
| [README.md](https://github.com/sherryberry001/microhydros/blob/main/README.md) | 261 | Syfte, hur systemet byggs och startas, beroenden, grundläggande användning |

**Beslutsunderlag (ADR).** Sex dokument där varje större tekniskt val har sin
egen motivering, med alternativen som övervägdes och varför de valdes bort:

| ADR | Fråga |
|---|---|
| [ADR-001](https://github.com/sherryberry001/microhydros/blob/main/docs/adr/ADR-001-sensorval.md) | Vilka givare, och varför inte DHT-familjen eller BME280 |
| [ADR-002](https://github.com/sherryberry001/microhydros/blob/main/docs/adr/ADR-002-kommunikation.md) | MQTT eller HTTP eller BLE eller seriellt |
| [ADR-003](https://github.com/sherryberry001/microhydros/blob/main/docs/adr/ADR-003-samplingsintervall.md) | Hur ofta mätdata bör samlas in, utifrån mätobjektens fysik |
| [ADR-004](https://github.com/sherryberry001/microhydros/blob/main/docs/adr/ADR-004-validering.md) | Hur felaktiga och orimliga mätvärden upptäcks |
| [ADR-005](https://github.com/sherryberry001/microhydros/blob/main/docs/adr/ADR-005-plattform.md) | ESP32 med Arduino-ramverk, och varför kärnan är hårdvarufri |
| [ADR-006](https://github.com/sherryberry001/microhydros/blob/main/docs/adr/ADR-006-forvarning.md) | Hur systemet varnar innan en skadlig nivå nås |

Arkitekturdiagrammet ligger som Mermaid i teknisk dokumentation och ritas ut
automatiskt när sidan öppnas på GitHub.

### 5. Testdokumentation

[docs/testrapport.md](https://github.com/sherryberry001/microhydros/blob/main/docs/testrapport.md), 477 rader.

Varje testfall redovisas med vad som testats, hur testet genomförts, förväntat
resultat och faktiskt resultat.

| Nivå | Testfall | Vad som täcks |
|---|---|---|
| Enhetstester | V-1 till V-17, T-1 till T-15, J-1 till J-9, S-1 till S-8, R-1 till R-8, P-1 till P-9 | Validering, trend, JSON, schemaläggning, ringbuffert, psykrometri |
| Integrationstester | I-1 till I-16 | Hela applikationen mot simulerad odling: normaldrift, felinjektion, nätavbrott, förvarning, sju dygns drift, millis-överflöde |
| Kedjetester | E2E-1 till E2E-4 | Mätvärde till broker till databas till panel, fyra scenarier |

Rapporten innehåller också ett avsnitt om **rimligheten i de mätvärden systemet
producerar**, och ett avsnitt med **tre fynd som gjordes under projektet**:

- **F-1:** Telemetriramen fick inte plats i den budgeterade bufferten. Enheten
  hade blivit helt tyst i fält utan att något loggades som fel.
- **F-2:** 41 falsklarm på sex timmar vid normal lampstart, vilket ledde till att
  förvarningsmodellen gjordes om.
- **F-3:** 3,6 procent av vattenmätningarna underkändes i felfri drift, vilket
  visade att en gräns var fel satt.

**Bevisfiler** från körningarna ligger i
[docs/evidence/](https://github.com/sherryberry001/microhydros/tree/main/docs/evidence):
loggar från alla fyra scenarier, databassammanfattning och skärmdumpar av
instrumentpanelen.

### 6. Demonstration och muntlig överlämning

[docs/demo.md](https://github.com/sherryberry001/microhydros/blob/main/docs/demo.md), 309 rader.

Demomanus med exakta kommandon i körordning, samt talpunkter för de sex delar
uppgiften kräver att presentationen täcker: hur uppdraget tolkats,
systemarkitekturen, viktiga tekniska val, resultatet, problem och begränsningar,
och rekommendationer för fortsatt utveckling.

Dokumentet innehåller också en lista med tänkbara frågor och svaren på dem.

### 7. Individuell reflektionsrapport

[docs/reflektion.md](https://github.com/sherryberry001/microhydros/blob/main/docs/reflektion.md), 286 rader.

Behandlar den egna rollen och arbetsinsatsen, viktiga tekniska och
organisatoriska beslut, hur arbetssättet fungerade, problem som uppstod och hur
de hanterades, vad som skulle gjorts annorlunda, hur arbetssättet passade
projektets förutsättningar, och vad jag tar med mig.

---

## Så verifierar du prototypen själv

Allt nedan går att köra utan hårdvara. Tar några minuter.

### Enhetstester

```bash
git clone https://github.com/sherryberry001/microhydros.git
cd microhydros
python3 -m venv .venv && .venv/bin/pip install platformio
cd firmware && ../.venv/bin/pio test -e native
```

Förväntat: `82 test cases: 82 succeeded`.

### Bygg firmware för målhårdvaran

```bash
../.venv/bin/pio run -e esp32dev
```

Förväntat: `SUCCESS`, RAM runt 24 procent och flash runt 60 procent.

### Hela kedjan: mätvärde till databas till panel

```bash
cd ../gateway
docker compose up -d --build

cd ../firmware && ../.venv/bin/pio run -e simnode
./.pio/build/simnode/program --scenario faults --hours 6 \
  | python3 ../gateway/bridge.py --stdin --host localhost
```

Öppna sedan **http://localhost:8088**. Panelen visar de sex mätvärdena, kurvor
per kanal, trend med förvarning, och en lista över underkända mätvärden med
orsak.

Fyra scenarier går att köra: `normal`, `faults`, `outage`, `heatwave`.

Fullständiga instruktioner i
[docs/demo.md](https://github.com/sherryberry001/microhydros/blob/main/docs/demo.md).

---

## Svar på kundens frågor

Kundbrevet avslutas med sex frågor. Samtliga besvaras i
[teknisk dokumentation, avsnitt 9](https://github.com/sherryberry001/microhydros/blob/main/docs/teknisk-dokumentation.md).

| Kundens fråga | Kort svar |
|---|---|
| Vilka sensorer är lämpliga? | SHT45 och SHT40-BD1B över I²C för luft, DS18B20 i vattentät sond för näringslösningen. Se ADR-001. |
| Hur ofta bör miljödata mätas? | Var tionde sekund, publicering varje minut. Motiverat utifrån mätobjektens tidskonstanter. Se ADR-003. |
| Hur kan mätdata struktureras och kommuniceras? | Versionerat JSON-schema med löpnummer och kvalitetsflagga per mätvärde, över MQTT. Se ADR-002. |
| Hur kan systemet upptäcka orimliga mätvärden? | Fem lager plus tre korskontroller mellan givare. Se ADR-004. |
| Hur bör en framtida produkt byggas för att kunna vidareutvecklas? | Hårdvarufri applikationskärna bakom tre gränssnitt, versionerat datakontrakt. Se ADR-005. |
| Vilka risker och begränsningar ser ni? | Simuleringsverifieringen först, därefter säkerheten, därefter långsam givardrift. Se avsnitt 10. |

---

## Filöversikt

```
microhydros/
├── README.md                        projektets syfte, bygge, beroenden, användning
├── INLAMNING.md                     det här dokumentet
├── docs/
│   ├── projektplan.md               mål, avgränsningar, backlog, tidsplan, risker
│   ├── krav.md                      20 spårbara krav med prioritet och testkoppling
│   ├── teknisk-dokumentation.md     arkitektur, hårdvara, datamodell, begränsningar
│   ├── testrapport.md               alla testfall, fynd, rimlighetsbedömning
│   ├── demo.md                      demomanus och presentationsstruktur
│   ├── reflektion.md                individuell reflektionsrapport
│   ├── adr/                         sex beslutsunderlag
│   └── evidence/                    loggar, databassammanfattning, skärmdumpar
├── firmware/
│   ├── platformio.ini               tre byggmiljöer: esp32dev, native, simnode
│   ├── src/                         main.cpp för ESP32, simnode_main.cpp för simulering
│   ├── lib/mh_core/                 applikationskärna, inga hårdvaruberoenden
│   ├── lib/mh_hw/                   SHT4x, DS18B20, MQTT, I²C-återställning
│   ├── lib/mh_sim/                  termisk modell och simulerade givare
│   └── test/                        sju testsviter, 82 testfall
└── gateway/
    ├── docker-compose.yml           broker, mottagare, panel
    ├── bridge.py                    seriell brygga till MQTT
    ├── ingest.py                    validering och lagring
    ├── dashboard.py                 instrumentpanel
    └── schema.sql                   tidsseriestruktur med kvalitetsflaggor
```
