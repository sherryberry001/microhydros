# Testrapport, MicroHydros

Prototyp 1.0. Alla siffror i det här dokumentet kommer från faktiska körningar
och går att reproducera med kommandona i [README](../README.md).

---

## 1. Sammanfattning

| | |
|---|---|
| Automatiserade tester | **82** |
| Resultat | **82 godkända, 0 underkända** |
| Körtid för hela sviten | **under 6 sekunder** |
| Scenariokörningar genom hela kedjan | 4, à 6 simulerade timmar |
| Mätvärden lagrade under scenariokörningarna | 7 862 |
| Fel som hittades och åtgärdades under projektet | 3 |

Testerna körs med:

```bash
cd firmware && pio test -e native
```

---

## 2. Teststrategi

### Vad som testas var

| Nivå | Vad som verifieras | Hur |
|---|---|---|
| Enhetstest | en klass i taget, med styrd tid och styrda fel | testdubbler |
| Integrationstest | hela applikationen mot en fysikalisk modell | simulerade givare, klockan stegas av testet |
| Kedjetest | mätvärde till databas | simuleringsnod → brygga → MQTT → mottagare → SQLite |

### Två principer som styrde testskrivandet

**För varje feltest finns ett motprov.** Ett filter som flaggar allt är precis
lika värdelöst som ett som flaggar ingenting, och det är lätt att bara testa
det första. Därför finns det för varje test som visar att ett fel upptäcks också
ett test som visar att en frisk givare inte larmar. Två av projektets tre fel
hittades av just motproven.

**Tiden styrs av testet.** Integrationstesterna använder en klocka som testet
stegar själv. Sju simulerade dygn tar under en sekund, vilket gör att beteenden
som annars bara syns efter lång drifttid går att verifiera i varje körning.

### Simuleringsmodellen

Testerna körs mot en fysikalisk modell av odlingsskåpet, inte mot slumpmässiga
tal. Modellen har:

| Storhet | Modell | Tidskonstant |
|---|---|---|
| Luft ute | dygnssvängning kring ett medelvärde | timmar |
| Luft inne | första ordningens lag mot ute plus lampans bidrag | 8 min |
| Vatten | första ordningens lag mot inneluften | 90 min |
| Fukt | absolut fuktighet med avdunstning driven av VPD, läckage mot rummet | – |

Att fukten modelleras som absolut fuktighet och räknas om till RH ger den rätta
kopplingen: när lampan tänder stiger temperaturen och RH faller, trots att
mängden vatten i luften är oförändrad. Det är samma beteende en odlare ser i sitt
skåp, och det gör att fuktkanalens validering testas mot något realistiskt.

Givarna brusar med sin datablads­specificerade nivå och kvantiserar till sin
upplösning. Slumpgeneratorn har fast frö, så varje körning ger identiska
siffror.

---

## 3. Enhetstester

### 3.1 Validering (V-1 till V-17)

Den mest testade delen av projektet, eftersom det var här kundens förra prototyp
föll.

| Id | Vad som testas | Hur | Förväntat | Faktiskt |
|---|---|---|---|---|
| V-1 | CRC-fel från drivrutinen | mata in `fail(ChecksumFailed)` | `bad` + `checksum_failed` | som förväntat |
| V-2 | Kretsen svarar inte | mata in `fail(NoResponse)` | `bad` + `no_response` | som förväntat |
| V-3 | Normalt värde | 22,5 °C till luftkanalen | `good` | som förväntat |
| V-4 | Utanför databladets mätområde | −60 °C mot SHT45:s −40 | `bad` + `out_of_sensor_range` | som förväntat |
| V-5 | NaN | 0/0 | `bad` | som förväntat |
| V-6 | Giltigt men orimligt | 70 °C i ett odlingsskåp | `suspect`, inte `bad` | som förväntat |
| V-7 | **Vattensond hoppar till 85 °C** | 21 → 85 °C på 10 s | `suspect` + `rate_of_change_exceeded` | som förväntat |
| V-8 | Långsam verklig förändring | 0,5 °C på en minut | `good`, inget larm | som förväntat |
| V-9 | **Spik flyttar inte referensen** | 21 → 85 → 21,1 | tredje värdet `good`, referens 21,1 | som förväntat |
| V-10 | **Sensorbrus larmar inte** | 2 000 avläsningar, brus 0,05 °C + kvantisering | 0 flaggade | 0 flaggade |
| V-11 | Brustillägget döljer inte verkliga hopp | 85 °C och 2 °C på 10 s | båda fångas | som förväntat |
| V-12 | Låst värde | 31 identiska avläsningar | `suspect` + `stuck_value` efter 30 | som förväntat |
| V-13 | Brus räknas inte som låst | 100 avläsningar med ±0,01 °C | aldrig `stuck_value` | som förväntat |
| V-14 | Daggpunkt över lufttemperatur | 20 °C luft, 23 °C daggpunkt | underkänns; 20/14 godkänns; 20/20,3 godkänns inom tolerans | som förväntat |
| V-15 | Lossnad vattensond | luft 24 °C, vatten 5 °C | korskontroll larmar | som förväntat |
| V-16 | Förväxlade luftgivare | inne 15 °C, ute 26 °C | korskontroll larmar | som förväntat |
| V-17 | Nollställning efter busståterställning | reset, sedan ett hopp | inget larm, historiken är borta | som förväntat |

V-7 är värd att lyfta. En DS18B20 som tappar kontakt rapporterar exakt +85,0 °C,
vilket är registervärdet efter reset. Talet ligger innanför mätområdet, ser helt
normalt ut, och fångas bara av derivatakontrollen som vet att några liter vatten
omöjligt kan bli 60 grader varmare på tio sekunder.

### 3.2 Trend och förvarning (T-1 till T-15)

| Id | Vad som testas | Förväntat | Faktiskt |
|---|---|---|---|
| T-1 | För få punkter | `unknown` | som förväntat |
| T-2 | Plan serie | `stable`, lutning 0 | som förväntat |
| T-3 | Känd lutning | 3,0 °C/h ger 3,0 ±0,05, r² > 0,999 | som förväntat |
| T-4 | Fallande serie | −4,0 °C/h ±0,05 | som förväntat |
| T-5 | Förvarningstid | 6 °C/h från 24 °C mot 30 °C ger ca 60 min | 60,2 min |
| T-6 | Rör sig bort från gränsen | ingen tid rapporteras | som förväntat |
| T-7 | Brus utan trend | `stable`, inget larm | som förväntat |
| T-8 | Kraftigt brus | låg förklaringsgrad, r² < 0,5 | som förväntat |
| T-9 | Fönstret glider | endast de senaste punkterna behålls | som förväntat |
| T-10 | **Normal lampstart** | 21 → 27,5 °C, jämvikt under gränsen, **ingen** förvarning | jämvikt skattad 27,5 ±1, tau 8 ±2 min, ingen varning |
| T-11 | **Skenande uppvärmning** | 24 → 38,5 °C, jämvikt över gränsen, förvarning ges | jämvikt 38,5 ±2, varning 1,4 min innan, ±1,5 |
| T-12 | Linjärt förlopp | ingen jämvikt att skatta, faller tillbaka på extrapolering | 15,2 min ±1,5 |
| T-13 | Redan över gränsen | `limit_exceeded`, ingen "tid till" | som förväntat |
| T-14 | **Fallande temperatur** | aldrig förvarning om en övre gräns | som förväntat |
| T-15 | Lutningskvot vid plan serie | neutral, 1,0 | som förväntat |

T-10 och T-11 är kärnan. De två fallen ser nästan likadana ut i mätdata: båda
stiger brant, båda bromsar in. Skillnaden är bara var de är på väg, och det är
exakt det modellen mäter. T-14 är ett regressionstest för ett fel som hittades i
kedjetestning, där en fallande temperatur rapporterades som "når 30 °C om 59
minuter".

### 3.3 JSON och telemetri (J-1 till J-9)

| Id | Vad som testas | Förväntat | Faktiskt |
|---|---|---|---|
| J-1 | Enkelt objekt | exakt strängmatchning | som förväntat |
| J-2 | Flyttal med decimaler | `22.46`, `-3.5` | som förväntat |
| J-3 | Ledande nollor i decimaldelen | `1.05` inte `1.5`, `0.007` | som förväntat |
| J-4 | NaN | blir `null`, aldrig `0` | som förväntat |
| J-5 | För liten buffert | returnerar 0, trunkerad ram skickas inte | som förväntat |
| J-6 | Specialtecken | escapas korrekt | som förväntat |
| J-7 | Hel telemetriram | alla fält finns, `bad` ger `null` med orsak kvar | som förväntat |
| J-8 | **Buffertbudget** | största möjliga ram får plats | **misslyckades först**, se fynd F-1 |
| J-9 | Statusram | `offline`, firmwareversion | som förväntat |

J-3 testar en klassisk bugg i handskrivna flyttalsformaterare, där 1,05 blir
"1.5" för att den ledande nollan i decimaldelen tappas bort.

### 3.4 Schemaläggare (S-1 till S-8)

| Id | Vad som testas | Förväntat | Faktiskt |
|---|---|---|---|
| S-1 | Uppgift förfaller efter sin period | exakt takt | som förväntat |
| S-2 | Flera takter samtidigt | båda förfaller i samma varv vid 5 000 ms | som förväntat |
| S-3 | Utspridd start | första fördröjningen respekteras | som förväntat |
| S-4 | Missade perioder körs inte ikapp | ett tick vid 60 s ger en körning, inte sextio | som förväntat |
| S-5 | **millis() slår runt** | fortsätter fungera förbi varvet | som förväntat |
| S-6 | Avstängd uppgift | förfaller inte | som förväntat |
| S-7 | Tid till nästa uppgift | korrekt | som förväntat |
| S-8 | Full tabell | avvisar ytterligare uppgifter | som förväntat |

S-5 provocerar fram överflödet genom att sätta klockan strax före 0xFFFFFFFF och
stega förbi. En schemaläggare skriven som `now >= deadline` klarar inte det, och
enheten slutar tyst skicka data efter 49,7 dygn. Det är ett fel som aldrig syns
på en demonstration.

### 3.5 Ringbuffert (R-1 till R-8) och psykrometri (P-1 till P-9)

Ringbufferten testas för FIFO-ordning, att överskrivning räknas, indexering från
äldst, blandad push och pop, och att den inte glider ur takt efter 10 000 varv.

Psykrometrin jämförs mot tabellvärden: mättnadsångtryck vid 0, 20 och 30 °C,
daggpunkt vid känd temperatur och fuktighet, och att daggpunkten alltid ligger
under lufttemperaturen över hela arbetsområdet, vilket är den egenskap
korskontrollen vilar på. VPD vid 22 °C och 65 %RH ger 0,93 kPa, alltså mitt i det
rekommenderade intervallet för sallad.

---

## 4. Integrationstester (I-1 till I-16)

Hela applikationen mot den simulerade odlingen.

### 4.1 Normaldrift

| Id | Vad som testas | Förväntat | Faktiskt |
|---|---|---|---|
| I-1 | Alla mätpunkter i ramen | fyra obligatoriska plus två härledda | 6 mätvärden, ≥9 ramar på 10 min |
| I-2 | **Inga underkända i felfri drift** | 0 `bad`, försumbart antal `suspect` | 0 `bad` |
| I-3 | Mätvärdena är fysiskt rimliga | efter ett dygn: ute 15–27 °C, inne 15–35 °C, vatten inom 8 °C från luften, RH 20–100 % | som förväntat |
| I-4 | **Vattnet släpar efter luften** | vid lampstart stiger luften > 2 °C och vattnet mindre än halva det | som förväntat |

I-4 är en fysikalisk kontroll snarare än en funktionell. Om vattnet skulle följa
luften direkt är antingen modellen eller tolkningen av mätvärdena fel, och då är
derivatagränsen för vatten satt på lösan grund.

### 4.2 Felinjektion

| Id | Injicerat fel | Förväntat | Faktiskt |
|---|---|---|---|
| I-5 | Vattensond rapporterar 85 °C | upptäcks av derivata eller korskontroll | `rate_of_change_exceeded` |
| I-6 | CRC-fel på fuktkanalen | `bad` + `null` i ramen, orsak bevarad | som förväntat |
| I-7 | Givare slutar svara | fem bussfel i rad utlöser omstart av givarna | omstart skedde |
| I-8 | Givare fastnar på samma värde | `suspect` + `stuck_value` efter 5 min | som förväntat |
| I-9 | **Felet försvinner** | systemet återhämtar sig, fastnar inte i larmläge | `good` igen efter 5 min |

I-9 är lika viktigt som de fyra ovanför. Ett system som upptäcker ett fel men
sedan aldrig kommer ur larmläget är inte användbart.

### 4.3 Kommunikationsavbrott

| Id | Vad som testas | Förväntat | Faktiskt |
|---|---|---|---|
| I-10 | Ramar buffras och skickas sedan | inget går ivag under avbrottet, allt kommer fram efter | kön tömd, ≥9 ramar levererade i efterhand |
| I-11 | Löpnummer utan hål | seq räknas upp med exakt ett per ram, även över avbrott | som förväntat |
| I-12 | **Kön blir full** | äldst tappas först, antalet rapporteras i `dropped_frames` | som förväntat |

### 4.4 Förvarning

| Id | Vad som testas | Förväntat | Faktiskt |
|---|---|---|---|
| I-13 | Trenden varnar innan gränsen nås | stigande riktning, positiv tid kvar medan vi ännu är under | som förväntat |
| I-14 | **Ingen förvarning vid normal fotoperiod** | den dagliga uppvärmningen får inte larma | `stable` när jämvikten nåtts |

### 4.5 Långtidsstabilitet

| Id | Vad som testas | Förväntat | Faktiskt |
|---|---|---|---|
| I-15 | **Sju dygns drift** | >240 000 avläsningar, 0 `bad`, >10 000 ramar, tom kö, klimatet inte glidit ivag | som förväntat |
| I-16 | **Drift förbi millis-varvet** | klockan startas 2 min före 0xFFFFFFFF och körs förbi | fortsatte publicera utan avbrott |

---

## 5. Kedjetester (E2E)

Fyra scenarier kördes genom hela kedjan: simuleringsnod → seriell brygga → MQTT
→ mottagare → SQLite. Varje scenario simulerar sex timmars drift och rapporterar
som en egen enhet.

```bash
firmware/.pio/build/simnode/program --scenario faults --hours 6 \
  | python3 gateway/bridge.py --stdin --host localhost
```

### E2E-1: Normaldrift

| Mått | Resultat |
|---|---|
| Avläsningar | 8 640 |
| Godkända | 8 564 |
| Underkända (`bad`) | 0 |
| Ramar byggda och skickade | 360 av 360 |
| **Underkända mätvärden i databasen** | **0 av 2 160 (0,00 %)** |
| Förvarningar | 0 |

Noll falsklarm under sex timmars felfri drift. Innan rättelsen i fynd F-3 var
samma siffra 3,6 procent.

### E2E-2: Injicerade givarfel

Fyra fel injiceras i tur och ordning och tas sedan bort igen.

| Injicerat fel | Upptäckt som | Antal |
|---|---|---|
| Vattensond rapporterar 85 °C | `suspect` / `rate_of_change_exceeded` | 5 |
| Fuktkanalen ger CRC-fel | `bad` / `checksum_failed` | 5 |
| Inneluften fastnar | `suspect` / `stuck_value` | 6 |
| Utegivaren slutar svara | `bad` / `no_response` | 5 |
| Härledda värden med tveksam indata | `suspect` / `derived_from_suspect` | 12 |

Alla fyra upptäcktes, med rätt orsakskod, och systemet återhämtade sig varje
gång felet togs bort. 12 bussomstarter utlöstes av de två bussfelen.

Vattenkanalens spann i databasen är `20.938 … 85.0`, vilket visar att det
felaktiga värdet **bevarades** med sin flagga istället för att tyst kastas bort.
Det syns också som en tydlig spik i instrumentpanelens kurva.

### E2E-3: Nätavbrott

| Mått | Resultat |
|---|---|
| Ramar byggda | 360 |
| Ramar levererade | 232 |
| Ramar tappade | 128 |
| Hål upptäckt av gatewayen | **ja: 128 ramar saknas mellan seq 144 och 273** |

Avbrottet varade längre än kön räcker till, vilket är avsiktligt i scenariot.
Poängen är att förlusten **blev synlig**: vyn `v_sequence_gaps` pekar ut exakt
vilka löpnummer som saknas. Utan löpnummer hade två timmars data försvunnit utan
att någon märkt det.

Vid ett avbrott kortare än sexton minuter tappas ingenting alls, vilket är
verifierat i I-10.

### E2E-4: Skenande uppvärmning

Klimatet står stilla i mörkerperioden. Sedan tänder lampan samtidigt som
kylningen slutar fungera, och jämvikten hamnar på cirka 33 °C istället för 27,5.

```
FORVARNING: air_in når 30.0 C om 7 min (39.61 C/h, på väg mot 32.5 C, r2 0.80)
FORVARNING: air_in når 30.0 C om 6 min (55.86 C/h, på väg mot 32.2 C, r2 0.93)
FORVARNING: air_in når 30.0 C om 3 min (66.00 C/h, på väg mot 34.4 C, r2 0.99)
FORVARNING: air_in når 30.0 C om 2 min (57.46 C/h, på väg mot 34.3 C, r2 0.99)
FORVARNING: air_in når 30.0 C om 1 min (50.73 C/h, på väg mot 34.2 C, r2 0.99)
GRANS OVERSKRIDEN: air_in ligger över 30.0 C just nu
```

| Mått | Resultat |
|---|---|
| Förvarningstid innan gränsen passerades | **7 minuter** |
| Skattad jämvikt | 32,2 till 34,4 °C |
| Verklig jämvikt | 33,5 °C |
| Falsklarm i de tre andra scenarierna | **0** |

---

## 6. Fynd under projektet

Tre fel hittades och åtgärdades. De är värda att redovisa i detalj, eftersom de
säger mer om vad testuppsättningen var värd än de 82 godkända testerna gör.

### F-1: Telemetriramen fick inte plats i bufferten

**Hur det hittades:** test J-8, som bara kontrollerar att den största möjliga
ramen får plats i den budgeterade bufferten.

**Vad som var fel:** `kMaxFrameLen` var satt till 640 byte. Den verkliga
worst-case-ramen är cirka 1 850 byte: åtta mätobjekt à ungefär 190 tecken med de
längsta strängvärdena schemat kan producera, plus huvud, trend och hälsotal.

**Vad som hade hänt:** `encodeTelemetry` returnerar 0 vid overflow, ingen ram
byggs, och enheten hade fortsatt mäta och validera helt korrekt **utan att skicka
ett enda värde**. Inget hade loggats som ett fel. Det är exakt den sortens tysta
fel kunden beskrev från sin förra leverantör.

**Åtgärd:** budgeten räknades fram istället för att uppskattas, och sattes till
2 048 byte med cirka 15 procents marginal. Kön minskades från 24 till 16 ramar så
att den statiska minnesbudgeten hamnade på 32 kB.

**Lärdom:** en konstant som är satt på känsla ska ha ett test som kontrollerar
den. Testet tog fem minuter att skriva.

### F-2: 41 falsklarm på sex timmar vid normal lampstart

**Hur det hittades:** första hela kedjekörningen av scenariot `normal`, alltså
ett scenario helt utan fel.

**Vad som var fel:** linjär extrapolering av trenden. När odlingslampan tänder
strävar lufttemperaturen mot en ny jämvikt exponentiellt. I början av det
förloppet är lutningen brant, runt 47 °C/h, och en rät linje pekar rakt genom
30 °C. I verkligheten planar kurvan ut på 27,5 °C.

Förklaringsgraden var 0,97. Statistiken var alltså rätt, modellen var fel.

**Första åtgärden var också fel.** Ett filter som undertryckte larm när förloppet
bromsade in tog bort falsklarmen, men hade också tystat en äkta skenande
uppvärmning, eftersom även den bromsar in på vägen mot sin jämvikt. Det upptäcktes
genom att pröva filtret mot båda fallen.

**Åtgärd:** firmwaren identifierar istället en första ordningens modell ur
trendfönstret och skattar var förloppet är på väg. Ligger jämvikten under gränsen
nås den aldrig, hur brant det än lutar just nu. Hela resonemanget i
[ADR-006](adr/ADR-006-forvarning.md).

Ett andra fel blev synligt i samma körning: systemet larmade med **negativ**
lutning, alltså när temperaturen var på väg ner genom 30 °C. Det är
återhämtningen efter ett problem, inte ett nytt problem. Regressionstest T-14.

Ett tredje: trendfönstret var 15 minuter medan skåpets tidskonstant är 8. När
något gick sönder innehöll fönstret fortfarande mest data från innan felet.
Fönstret kortades till 6 minuter och **förvarningstiden gick från 1 minut till
7**.

**Resultat:** 41 falsklarm → 0. Förvarningen fungerar.

**Lärdom:** en bra statistisk anpassning är inte samma sak som en riktig modell,
och ett filter måste prövas mot både falsklarmet och det äkta larmet.

### F-3: 3,6 procent av vattenmätningarna underkända i felfri drift

**Hur det hittades:** en databasfråga efter kedjekörningen, som grupperade
avvikelser per givare och orsak. Enhetstesterna hade släppt igenom det eftersom
andelen låg strax under det tröskelvärde I-2 kontrollerade.

**Vad som var fel:** derivatakontrollen jämförde ren hastighet mot gränsen. En
DS18B20 brusar 0,05 °C och kvantiserar i steg om 0,0625 °C, så två avläsningar
tio sekunder isär skiljer sig typiskt 0,07 °C av rena mätskäl. Omräknat blir det
0,42 °C per minut, och med en gräns på 1,0 räckte ett par standardavvikelser för
att slå i taket.

Den fysikaliska uträkningen bakom gränsen var korrekt. Den tog bara inte hänsyn
till mätosäkerheten hos givaren som mäter fysiken.

**Åtgärd:** jämför absoluta steg istället för hastigheter, och lägg till
givarens brus som en konstant:

```
tillåtet steg = max_rate_per_min × dt_minuter + rate_noise_allowance
```

**Resultat:** 3,6 % → **0,00 %**, och det verkliga felet har fortfarande över
hundra gånger marginalen kvar. Låst med regressionstesterna V-10 och V-11.

**Lärdom:** en gräns härledd ur fysiken måste ändå ta hänsyn till mätosäkerheten.
Och: enhetstester med trösklar kan dölja ett problem som en fråga mot verklig
data visar direkt.

---

## 7. Rimlighetsbedömning av mätvärdena

Uppgiften efterfrågar ett resonemang om rimligheten i de värden systemet
producerar.

### En ram från normaldrift

| Storhet | Värde | Bedömning |
|---|---|---|
| Luft inne | 27,78 °C | Rummet är 21,4 °C, lampan tillför drygt 6 °C. Stämmer med en LED-armatur på några tiotal watt i ett litet skåp. |
| Luft ute | 21,35 °C | Normal rumstemperatur. |
| Vatten | 23,25 °C | Ligger mellan rummet och skåpsluften, vilket är rätt: vattnet strävar mot luften men släpar 90 minuter efter. |
| Fukt inne | 62,78 %RH | Slutet skåp med öppet vattenkärl. Rimligt, och i det intervall en odlare siktar på. |
| Daggpunkt | 20,04 °C | 7,7 °C under lufttemperaturen, alltså ingen kondensrisk. |
| VPD | 1,387 kPa | Strax över det rekommenderade 0,8 till 1,2 för sallad, vilket är förväntat när lampan värmt upp luften. |

### Varför den interna konsistensen säger mest

Ett enskilt tal kan vara fel utan att man ser det. Sex storheter som alla hänger
ihop fysikaliskt är mycket svårare att få rätt av misstag:

- **Daggpunkten ligger under lufttemperaturen.** Måste den göra per definition.
- **Vattnet ligger mellan rummet och skåpsluften.** Följer av tidskonstanterna.
- **VPD stämmer med temperatur och fuktighet.** Två oberoende mätningar och en
  formel som binder dem.
- **Inne är varmare än ute när lampan lyser.** Energibalansen kräver det.
- **RH faller när lampan tänder** trots att vattenmängden i luften är oförändrad.
  Det är mättnadsångtrycket som stiger med temperaturen.

### Vad rimlighetsbedömningen inte kan säga

Den säger att värdena är **inbördes konsistenta och fysikaliskt trovärdiga**. Den
säger **inte** att en verklig SHT45 i ett verkligt skåp visar 27,78 °C, eftersom
mätvärdena kommer från en modell och inte från en givare.

Modellens parametrar är valda efter rimliga antaganden om ett litet odlingsskåp,
men de är antaganden. Det är därför hårdvaruverifiering är första punkten i
rekommendationerna för nästa fas, och det konkreta första provet där bör vara att
mäta de verkliga tidskonstanterna och jämföra mot modellens 8 och 90 minuter.

---

## 8. Det som inte är testat

Ärligt redovisat.

| Område | Varför inte | Risk |
|---|---|---|
| I²C mot fysisk krets | ingen hårdvara | Medel. Protokollet följer databladet men timing och kabelkapacitans är oprövade. |
| 1-Wire mot fysisk DS18B20 | ingen hårdvara | Medel. Samma sak, och 1-Wire är känsligare för timing. |
| Verklig kondens på givare | kräver hårdvara och klimatskåp | Medel. Det var ett av kundens ursprungliga problem. |
| ESP32:ns egenuppvärmning | kräver hårdvara | Låg till medel. Kan ge systematiskt fel på den inre luftgivaren om de monteras nära. |
| WiFi-återanslutning i verkligheten | kräver hårdvara | Låg. Logiken är testad mot en transportdubbel. |
| Drift längre än sju simulerade dygn | tidsbudget | Låg. Sju dygn täcker millis-varvet och kö-beteendet. |
| Säkerhet | medvetet utanför omfattningen | Hög i skarp drift, dokumenterad som öppen punkt. |
| Flera enheter samtidigt mot en broker | kördes sekventiellt, inte parallellt | Låg. Gatewayen håller isär dem på `device_id`, vilket syns i databasen. |
