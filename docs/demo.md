# Demonstration och överlämning

Manus för genomgången med beställaren. Räkna med 15 till 20 minuter plus frågor.

Allt nedan går att köra live. Kommandona står med, och varje steg har en
uppskattad tid så att det går att korta ner om det behövs.

---

## Innan du börjar

```bash
cd gateway
docker compose down -v          # ren databas
docker compose up -d --build
cd ../firmware
../.venv/bin/pio run -e simnode  # simnode byggs inte av pio test
```

Ha tre fönster öppna:

1. Terminal för kommandon
2. Terminal med `docker compose logs -f ingest`
3. Webbläsare på <http://localhost:8088>

---

## 1. Hur uppdraget tolkades (2 min)

Börja inte med tekniken. Börja med den mening i kundbrevet som styrde allt:

> "Resultatet fungerade vid demonstration, men visade sig vara betydligt mindre
> tillförlitligt när systemet testades under längre tid."

Säg rakt ut vad den tolkades som: **det här är inte en beställning av fler
funktioner, det är en beställning av tillförlitlighet.** Ett system vars
mätvärden inte går att lita på över tid är oanvändbart oavsett hur många
funktioner det har.

Det fick tre konsekvenser som går igen i hela lösningen:

| Tolkning | Konsekvens |
|---|---|
| Givare måste ha felkontroll på gränssnittet | SHT4x och DS18B20, båda med CRC8 på varje mätvärde |
| Ett mätvärde är aldrig bara ett tal | varje mätvärde bär kvalitet och orsakskod |
| Fel som bara syns efter veckor måste gå att hitta på minuter | simuleringsmodell där dygn kan köras på sekunder |

---

## 2. Arkitektur (3 min)

Visa diagrammet i [teknisk-dokumentation.md](teknisk-dokumentation.md#2-systemarkitektur).

Det enda som verkligen behöver landa är gränsen mellan lagren:

> Applikationslogiken innehåller inte en enda rad hårdvarukod. Den känner till
> fyra gränssnitt, inget mer. Därför kör exakt samma kod på ESP32:n, mot
> simulerade givare, och i enhetstesterna.

Säg varför det spelar roll för kunden och inte bara för utvecklaren:

- Ett byte av givare eller mikrokontroller berör bara drivrutinslagret.
- Logiken kunde verifieras systematiskt utan att en trasig kabel blandade sig i
  felsökningen.
- **Tre verkliga fel hittades innan någon hårdvara var inblandad.**

---

## 3. Det kör (3 min)

```bash
cd gateway
../firmware/.pio/build/simnode/program --scenario normal --hours 6 \
  | ../.venv/bin/python bridge.py --stdin --host localhost
```

Växla till panelen. Peka på:

- **Sex mätvärden**, de fyra obligatoriska plus daggpunkt och VPD
- **Kurvorna**, där lampans fotoperiod syns som en tydlig dygnsrytm
- **Att RH faller när temperaturen stiger**, trots att vattenmängden i luften är
  oförändrad. Det är mättnadsångtrycket som ändras, och det är samma beteende en
  odlare ser i sitt skåp.

Visa sedan en rå ram:

```bash
../firmware/.pio/build/simnode/program --scenario normal --hours 1 2>/dev/null \
  | tail -1 | cut -d' ' -f3- | python3 -m json.tool
```

Peka på tre fält och säg varför de finns:

| Fält | Varför |
|---|---|
| `schema_version` | Mottagaren kan avgöra om den förstår ramen. Utan det blir varje framtida fältändring en utrullning där enhet och backend måste bytas samtidigt. |
| `seq` | Utan löpnummer är tappad data osynlig. |
| `quality` + `reason` | Skillnaden mellan `checksum_failed` och `no_response` är skillnaden mellan "se över kabeln" och "givaren är borta". |

---

## 4. Vad som händer när något går sönder (5 min)

**Det här är demonstrationens tyngdpunkt.** Det är här kundens faktiska problem
adresseras.

Säg först varför det är svårt:

> En trasig givare slutar nästan aldrig svara. Den fortsätter svara, men fel. En
> DS18B20 som tappar kontakt rapporterar exakt +85,0 °C, vilket är dess
> registervärde efter reset. Talet ligger innanför mätområdet och ser helt normalt
> ut. Ingen enkel rimlighetskontroll fångar det.

Kör felscenariot:

```bash
../firmware/.pio/build/simnode/program --scenario faults --hours 6 \
  | ../.venv/bin/python bridge.py --stdin --host localhost
```

Följ mottagarens logg medan det kör. Fyra fel injiceras i tur och ordning:

| Injicerat fel | Upptäcks som | Vilket lager |
|---|---|---|
| Vattensond rapporterar 85 °C | `rate_of_change_exceeded` | derivata |
| Fuktkanalen ger CRC-fel | `checksum_failed` | transport |
| Inneluften fastnar på samma värde | `stuck_value` | låst värde |
| Utegivaren slutar svara | `no_response` | transport, utlöser omstart av bussen |

Växla till panelen och välj `microhydros-sim-faults`. Peka på:

- **Spiken i vattenkurvan.** Det felaktiga värdet finns kvar i historiken med sin
  flagga. Det kastades inte bort.
- **Tabellen med avvikelser**, där varje underkänt mätvärde har en orsakskod.
- **Att härledda värden ärver kvaliteten** från sina indata,
  `derived_from_suspect`.

Säg varför felaktiga värden bevaras:

> Vi kastar aldrig ett mätvärde. Vi markerar det. Då kan grafen rita ett hål där
> data saknas istället för att dra en rak linje över ett fel som ingen upptäckte.
> Den raka linjen är precis hur ett system kan se friskt ut samtidigt som det
> inte är det.

Och visa att systemet återhämtar sig: när felen tas bort återgår kanalerna till
`good`. Ett system som fastnar i larmläge är inte användbart.

---

## 5. Nätavbrott (2 min)

```bash
../firmware/.pio/build/simnode/program --scenario outage --hours 6 \
  | ../.venv/bin/python bridge.py --stdin --host localhost
```

Panelen, enhet `microhydros-sim-outage`, under Dataförlust:

> 128 ram(ar) saknas mellan 144 och 273

Säg vad det visar:

> Avbrottet i scenariot är längre än kön räcker till, och det är avsiktligt.
> Poängen är inte att ingenting går förlorat. Poängen är att förlusten blir
> **synlig**, och att vi kan peka ut exakt vilka löpnummer som saknas. Vid ett
> avbrott kortare än sexton minuter tappas ingenting alls.

---

## 6. Förvarningen (3 min)

Det här svarar på kundens önskemål om att bli varnad innan skadan sker.

```bash
../firmware/.pio/build/simnode/program --scenario heatwave --hours 6 \
  | ../.venv/bin/python bridge.py --stdin --host localhost
```

Loggen ger:

```
FORVARNING: air_in når 30.0 C om 7 min (39.61 C/h, på väg mot 32.5 C, r2 0.80)
FORVARNING: air_in når 30.0 C om 6 min (55.86 C/h, på väg mot 32.2 C, r2 0.93)
...
GRANS OVERSKRIDEN: air_in ligger över 30.0 C just nu
```

**Sju minuters förvarning.** Den skattade jämvikten 32,5 °C mot den verkliga
33,5 °C.

Berätta sedan varför det inte blev rätt första gången, eftersom det är den mest
intressanta delen:

> Första versionen drog en rät linje genom mätpunkterna och räknade ut när den
> skär 30 °C. Den larmade 41 gånger på sex timmar, i ett scenario helt utan fel.
> Det var odlingslampan som tände.
>
> När lampan tänder stiger temperaturen brant, runt 47 grader per timme i början.
> En rät linje pekar rakt genom 30. I verkligheten planar kurvan ut på 27,5 och
> gränsen nås aldrig. Förklaringsgraden var 0,97, så statistiken var rätt.
> Modellen var fel.

Och varför den självklara rättelsen också var fel:

> Nästa försök var att undertrycka larm när förloppet bromsar in. Det tog bort
> falsklarmen. Men det hade också tystat ett äkta fel, eftersom en skenande
> uppvärmning mot 38 grader också bromsar in på vägen dit. Ett filter som tar
> bort falsklarmen genom att också ta bort de äkta larmen är sämre än inget
> filter alls.

Och vad som fungerade:

> Lösningen var att använda fysiken vi redan kände till. Luften i ett skåp är ett
> första ordningens system, så lutningen bär information om var förloppet är på
> väg. Mäter man lutningen i fönstrets två halvor går jämvikten att räkna ut.
> Ligger den under gränsen nås gränsen aldrig, hur brant det än lutar just nu.
>
> 41 falsklarm blev noll. Förvarningen blev sju minuter.

Visa gärna fältet `projected_equilibrium_c` i panelen under Trend och hälsa.

---

## 7. Testerna (2 min)

```bash
cd ../firmware && ../.venv/bin/pio test -e native
```

82 tester på under sex sekunder, inklusive sju simulerade dygns drift.

Lyft en princip och ett fynd.

**Principen:** för varje test som visar att ett fel upptäcks finns ett motprov
som visar att en frisk givare inte larmar. Ett filter som flaggar allt är lika
värdelöst som ett som flaggar ingenting, och två av projektets tre fel hittades
av just motproven.

**Fyndet:** ett test som bara kontrollerar att den största möjliga ramen får
plats i bufferten avslöjade att budgeten var satt till 640 byte när den verkliga
ramen behöver 1 850. Konsekvensen hade varit att enheten mätte och validerade
helt korrekt men **inte skickade ett enda värde**, utan att något loggades som
ett fel. Det är exakt den sortens tysta fel som gör att ett system ser friskt ut
tills någon saknar datan.

---

## 8. Begränsningar (2 min)

Säg det rakt ut och först, utan att vänta på frågan:

> Den viktigaste begränsningen är att prototypen är verifierad mot en fysikalisk
> simuleringsmodell och inte mot monterade givare. Firmwaren kompilerar för ESP32
> och drivrutinerna är skrivna mot databladen, men I²C-timing, kabellängder och
> verklig kondens är inte provade.
>
> Vad vi vet är att applikationslogiken fungerar, och det är samma kod som ligger
> på kortet. Vad vi inte vet är om bussen fungerar med den kabellängd som väljs,
> och om de antagna tidskonstanterna stämmer för ett verkligt skåp.
> Valideringsgränserna vilar på dem.

Sedan de tre viktigaste av de övriga:

| Begränsning | Allvar | Åtgärd |
|---|---|---|
| Ingen TLS, öppen broker, WiFi-lösenord i koden | Hög i skarp drift | TLS plus certifikat per enhet |
| QoS 0, ramar kan försvinna utan att någon säger till | Medel | ESP-IDF:s MQTT-klient ger QoS 1 |
| Långsam givardrift upptäcks inte | Medel | referensgivare eller statistik över månader i molnet |

Den sista är värd en kommentar: långsam drift var sannolikt en del av problemet
med den förra prototypen, och den löser vi **inte**. Varje enskild avläsning från
en givare som glider en tiondels grad i månaden är rimlig. Det kräver antingen en
referens att jämföra mot eller flera månaders historik.

---

## 9. Nästa fas (1 min)

I prioritetsordning:

1. **Verifiera mot fysisk hårdvara.** Montera givarna, kör ett dygn, mät de
   verkliga tidskonstanterna och jämför mot modellens 8 och 90 minuter. Allt
   annat bör vänta tills det är gjort, eftersom resultatet kan påverka
   valideringsgränserna.
2. **Säkerheten.** TLS, certifikat per enhet, behörighetsregler på brokern,
   WiFi-uppgifter ur koden och in i NVS.
3. **Leveransgaranti.** QoS 1 via ESP-IDF:s MQTT-klient.
4. Därefter: flytta långtidsanalysen till molnet, lägg till pH och ledningstal,
   aggregera min/max/medel per minut, gör gränsvärdena inställbara per odling.

Datamodellen är redan byggd för punkt 4. En ny givare kräver ingen migrering,
eftersom lagringen har en rad per mätvärde och inte en rad per ram.

---

## Frågor du bör kunna svara på

| Fråga | Kort svar |
|---|---|
| Varför inte DHT22? | Inget riktigt CRC, tidskritiskt protokoll som stör ut av wifi-avbrott, och ospecificerad långtidsdrift. Det sista stämmer med vad kunden faktiskt upplevde. |
| Varför två olika SHT-kretsar? | Bästa noggrannhet där besluten fattas, enklare variant där kravet bara är en referens. Samma drivrutin, och olika I²C-adress så de kan dela buss. |
| Varför inte en noggrannare vattengivare? | Den måste sitta i vätskan, vilket utesluter allt som inte finns i en färdig tät sond. ±0,5 °C räcker när det som spelar roll är om lösningen ligger rätt och hur den rör sig. |
| Varför tio sekunder och inte en minut? | Inte för klimatets skull utan för valideringens. Ju tätare mätpunkterna ligger, desto mindre utrymme finns för ett felaktigt värde att se ut som en verklig förändring. |
| Varför skickas inte varje mätvärde? | En ram i minuten räcker för att beskriva klimatet, och molnplattformar tar betalt per meddelande. Den täta mätningen går inte förlorad, den används för valideringen. |
| Varför MQTT och inte HTTP? | Kunden har inte valt plattform. Med MQTT känner enheten bara till en broker. Byte av mottagare blir en adressändring istället för en ombyggnad. |
| Vad händer om wifi försvinner? | Enheten köar upp till sexton ramar och skickar dem när nätet kommer tillbaka. Blir avbrottet längre tappas äldst först, och antalet rapporteras så att förlusten syns. |
| Hur vet ni att mätvärdena är rimliga? | Genom den inbördes konsistensen. Daggpunkten ligger alltid under lufttemperaturen, vattnet mellan rummet och skåpsluften, och RH faller när lampan tänder. Sex storheter som alla hänger ihop fysikaliskt är svåra att få rätt av misstag. |
| Varför ligger fysiken i firmwaren och larmpolicyn i gatewayen? | Enheten är den enda som ser hela serien och den enda som fungerar utan uppkoppling. Men vilken marginal som är värd att väcka någon för beror på odlingen, och ska gå att ändra utan att flasha om enheter. |
| Vad skulle ni göra annorlunda? | Skaffa hårdvara tidigare, och börja köra hela kedjan tidigare än vecka tre. Alla tre felen hittades av att systemet fick köra, inte av att jag läste koden. |
