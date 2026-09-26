# Kravspecifikation, MicroHydros

Kraven är härledda ur kundbrevet från HydroGreen Fingers och ur
projektbeskrivningens tekniska krav. Kundbrevet är medvetet skrivet som ett
kundunderlag och inte som en kravspecifikation, så en del av arbetet har varit
att tolka det.

Varje krav har en spårning till var det är uppfyllt och till vilket test som
visar det. Testidentifierarna följer [testrapport.md](testrapport.md).

**Prioritering:** M = Must, S = Should, C = Could.

---

## 1. Funktionella krav

### Mätning

| Id | Krav | Pri | Källa | Uppfyllt i | Test |
|---|---|---|---|---|---|
| FR-1 | Systemet ska mäta lufttemperatur inne i odlingsutrymmet | M | Kundbrev, uppgift 3.1 | `Sht4xTemperature` på 0x44 | I-1, I-3 |
| FR-2 | Systemet ska mäta lufttemperatur utanför odlingsutrymmet | M | Kundbrev, uppgift 3.1 | `Sht4xTemperature` på 0x45 | I-1, I-3 |
| FR-3 | Systemet ska mäta temperaturen i vatten eller näringslösning | M | Kundbrev, uppgift 3.1 | `Ds18b20Sensor` | I-1, I-3 |
| FR-4 | Systemet ska mäta relativ luftfuktighet inne | M | Kundbrev, uppgift 3.1 | `Sht4xHumidity` på 0x44 | I-1, I-3 |
| FR-5 | Mätningar ska genomföras återkommande så att en tidsserie kan byggas | M | Uppgift 3.1 | `Scheduler`, 10 s | S-1, I-1 |

### Databehandling

| Id | Krav | Pri | Källa | Uppfyllt i | Test |
|---|---|---|---|---|---|
| FR-6 | Systemet ska upptäcka felaktiga och orimliga mätvärden | M | Uppgift 9.5, kundbrev | `ChannelValidator`, fem lager | V-1 till V-17 |
| FR-9 | Systemet bör upptäcka att ett mätvärde är på väg mot en skadlig nivå och varna i förväg | S | Kundbrev | `TrendEstimator` | T-5 till T-14, I-13 |
| FR-10 | Systemet bör räkna fram daggpunkt och VPD | C | Tolkning av odlingsbehov | `psychro::` | P-1 till P-9 |
| FR-11 | Härledda värden ska ärva sämsta kvalitet från sina indata | S | Egen slutsats | `MicroHydrosApp::buildAndDispatchFrame` | I-6 |

### Kommunikation

| Id | Krav | Pri | Källa | Uppfyllt i | Test |
|---|---|---|---|---|---|
| FR-7 | Mätdata ska kunna kommuniceras till ett externt system | M | Uppgift 9.4, kundbrev | `MqttTransport`, `LineTransport` | I-1, E2E-1 |
| FR-8 | Mätdata ska inte gå förlorad vid kortare kommunikationsavbrott | S | Kundbrev om historik | `Outbox`, 16 ramar | I-10, I-12, E2E-3 |
| FR-12 | Dataförlust som ändå sker ska vara synlig | S | Egen slutsats | `seq`, `dropped_frames`, `v_sequence_gaps` | I-11, I-12, E2E-3 |

---

## 2. Icke-funktionella krav

| Id | Krav | Pri | Källa | Uppfyllt i | Test |
|---|---|---|---|---|---|
| NFR-1 | Mätfrekvensen ska vara motiverad utifrån mätobjektets fysik | M | Kundens fråga | [ADR-003](adr/ADR-003-samplingsintervall.md) | S-1 |
| NFR-2 | Val av givare ska motiveras utifrån projektets behov, och DHT11/DHT22 får inte användas | M | Kundbrev, uppgift 3.1 | [ADR-001](adr/ADR-001-sensorval.md) | granskning |
| NFR-3 | Lösningen ska vara dokumenterad så att en annan utvecklare kan förstå uppbyggnaden | M | Uppgift 9.6 | teknisk dokumentation + 6 ADR | granskning |
| NFR-4 | Kommunikationslösningen ska inte låsa fast kunden vid en plattform | M | Kundbrev | MQTT, versionerat schema | E2E-1 |
| NFR-5 | Systemet ska vara stabilt över lång drifttid | M | Kundbrev | ingen heap i drift, watchdog, overflow-säker tid | S-5, I-15, I-16 |
| NFR-6 | Kommunikationen ska vara skyddad mot avlyssning och förfalskning | S | God praxis | **Delvis.** Endast förberedd. | saknas |
| NFR-7 | Systemet ska fortsätta fungera delvis när en givare är trasig | S | Egen slutsats | `begin()` returnerar men avbryter inte | I-7, I-9 |
| NFR-8 | Logiken ska kunna verifieras utan tillgång till hårdvara | S | Riskåtgärd R1 | `mh_core` utan hårdvaruberoenden | hela sviten |

### Om NFR-6

Kravet är **inte uppfyllt** i prototypen. Brokern körs öppen, utan TLS och utan
autentisering per enhet, och WiFi-lösenordet ligger som en kompilerad konstant.

Det är ett medvetet val för en prototyp på ett isolerat labbnät, men det får inte
förväxlas med att problemet är löst. Det som saknas och vad som krävs står i
[ADR-002](adr/ADR-002-kommunikation.md#känd-begränsning-säkerhet) och i
rekommendationerna för nästa fas. Raderna som behövs ligger förberedda och
bortkommenterade i `mosquitto.conf`, så att det inte ska behöva letas upp från
början.

---

## 3. Projektbeskrivningens tekniska krav för godkänt

| Krav i uppgift 9 | Uppfyllt | Var |
|---|---|---|
| 1. Minst fyra efterfrågade mätpunkter kan läsas in | Ja | FR-1 till FR-4, plus två härledda |
| 2. Mätningar genomförs återkommande | Ja | var tionde sekund, verifierat över sju simulerade dygn |
| 3. Mätdata behandlas av ett embedded-system | Ja | validering, trendmodell och ramsättning sker ombord |
| 4. Mätdata kan kommuniceras till externt system | Ja | MQTT till Mosquitto, lagring i SQLite |
| 5. Systemet har hantering av felaktiga eller orimliga situationer | Ja | fem lager, korskontroller, busståterställning, watchdog, store-and-forward |
| 6. Lösningen är dokumenterad så att en annan utvecklare förstår uppbyggnaden | Ja | teknisk dokumentation med arkitekturdiagram, sex beslutsunderlag |

---

## 4. Hur kundbrevet tolkades

Några ställen där kundbrevet krävde tolkning, och vad tolkningen blev.

### "Mätningarna ska kunna genomföras återkommande över tid"

Kunden skriver också att det inte räcker att läsa av ett aktuellt värde, och att
de vill kunna analysera hur klimatet utvecklas.

**Tolkning:** kravet gäller inte bara att mäta ofta utan att datamodellen ska
vara byggd för tidsserier från början. Det gav två konkreta konsekvenser:
monotona löpnummer så att luckor blir synliga, och en lagringsmodell med en rad
per mätvärde istället för en rad per ram.

### "Varna användaren innan temperaturen når en nivå som riskerar att skada växterna"

Kunden säger samtidigt att vi inte behöver utveckla hela analysfunktionen.

**Tolkning:** bygg en enkel men fungerande version, inte en placeholder.
Motiveringen är att det är den funktion som tydligast skiljer ett mätsystem från
ett övervakningssystem, och att den avslöjar om datamodellen duger, eftersom den
kräver en serie och inte ett värde. Det visade sig stämma: arbetet med
förvarningen avslöjade två fel som annars inte hade upptäckts.

### "Vi upplevde problem med mätkvalitet, stabilitet och hur väl komponenterna passade den miljö där produkten ska användas"

**Tolkning:** det här är inte tre separata klagomål utan ett. Ett system vars
mätvärden inte går att lita på över tid är oanvändbart oavsett hur många
funktioner det har. Därför prioriterades validering och kvalitetsmärkning över
allt annat, och därför är valideringen den mest testade delen av projektet.

### "Vi vill inte heller att komponenter väljs enbart för att de råkar finnas nära till hands"

**Tolkning:** kunden vill se att alternativen faktiskt jämförts, inte bara att
slutvalet är bra. [ADR-001](adr/ADR-001-sensorval.md) innehåller därför både de
kretsar som valdes bort och skälen, inklusive ett fall där en tekniskt bättre
krets valdes bort av tillgänglighetsskäl.

### Vad som inte gick att tolka fram

Kunden anger ingen skadlig temperaturnivå. Vi satte 30 °C för lufttemperaturen
inne, baserat på att sallad och de flesta bladörter börjar få problem över det i
ett slutet skåp. Det är ett antagande, det är markerat som ett antagande, och
gränsen bör vara inställbar per odling i nästa fas.
