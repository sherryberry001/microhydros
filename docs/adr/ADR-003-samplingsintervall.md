# ADR-003: Samplingsintervall och publiceringstakt

**Status:** Beslutad
**Datum:** vecka 2
**Berör:** krav FR-5, NFR-1
**Svarar på kundens fråga:** "Hur ofta bör miljödata mätas?"

## Sammanhang

Kunden frågar rakt ut hur ofta miljödata bör mätas. Frågan har inget självklart
svar, men den har ett svar som går att räkna fram, och det är bättre än att välja
en siffra som känns lagom.

Två takter behöver bestämmas, och de är inte samma sak:

- Hur ofta **läses givarna**
- Hur ofta **skickas data vidare**

## Vad fysiken kräver

Det som begränsar hur ofta det är meningsfullt att mäta är hur snabbt det som
mäts faktiskt kan ändra sig. För varje kanal finns en termisk tidskonstant, alltså
den tid det tar att nå 63 procent av vägen till ett nytt jämviktsläge.

| Kanal | Volym och massa | Tidskonstant | Snabbaste rimliga förlopp |
|---|---|---|---|
| Luft inne | några tiotal liter i ett otätt skåp | ca 8 min | lampan tänder, 6 °C på en kvart |
| Luft ute | rummets volym | timmar | dörr öppnas, 1 °C på några minuter |
| Vatten | några liter näringslösning | ca 90 min | ingen, vattnet är tröghetens definition |
| Fukt inne | kopplad till lufttemperaturen | ca 8 min | lucka öppnas, 20 %RH på en minut |

Tumregeln från reglerteknik och signalbehandling är att man vill ha
storleksordningen tio mätpunkter per tidskonstant för att kunna beskriva ett
förlopp ordentligt. Med den snabbaste kanalen på åtta minuter ger det ungefär
femtio sekunder mellan mätningarna.

Vi valde tio sekunder, alltså fem gånger tätare än vad den regeln kräver. Skälet
är inte fysiken utan valideringen.

## Vad valideringen kräver

Derivatakontrollen i [ADR-004](ADR-004-validering.md) avgör om en förändring är
fysiskt möjlig. Den kontrollen blir bättre ju tätare mätningarna ligger, av ett
enkelt skäl: ett felaktigt värde som dyker upp mellan två mätningar med en minuts
mellanrum kan vara en riktig, snabb förändring. Samma värde mellan två mätningar
med tio sekunders mellanrum är omöjligt.

Konkret: vattnets gräns är 1 °C per minut. Vid en minuts intervall tillåts alltså
ett steg på 1 °C, och ett fel som ligger inom det slinker igenom. Vid tio
sekunders intervall tillåts 0,17 °C, och samma fel fångas.

Detektionen av låsta värden blir också meningsfull först vid tät sampling. Trettio
identiska avläsningar i rad är ett starkt tecken vid tio sekunders intervall, där
det motsvarar fem minuter. Vid en minuts intervall hade det tagit en halvtimme
innan en hängd buss upptäcktes.

## Vad kostnaden är

Tio sekunder mellan avläsningarna betyder 8 640 avläsningar per kanal och dygn.
Det låter mycket, men:

- **Energi.** En SHT45-mätning tar 8,2 ms och drar ca 500 µA. Vid tio sekunders
  intervall blir medelströmmen under en mikroampere. Enheten är dessutom
  nätansluten, den sitter i samma uttag som lampan och pumpen.
- **Slitage.** Inget. Ingenting skrivs till flash i normal drift.
- **Egenuppvärmning.** Databladet varnar för att täta mätningar värmer kretsen.
  Vid tio sekunders intervall är arbetscykeln under en promille, så bidraget är
  försumbart. Det är också skälet till att båda kanalerna på en SHT45 läses i
  samma mätning istället för var för sig, se `Sht4xDevice`.
- **Nät och lagring.** Ingenting alls, eftersom mätningarna inte skickas en och
  en.

## Beslut

| Takt | Intervall | Vad som händer |
|---|---|---|
| Avläsning | **10 s** | alla fyra kanaler läses, valideras, trendfönstret matas |
| Publicering | **60 s** | en telemetriram byggs och skickas |
| Statusram | **300 s** | livstecken, publiceras retained |

Trendfönstret är 36 avläsningar, alltså sex minuter. Motiveringen för den siffran
står i [ADR-006](ADR-006-forvarning.md), eftersom den hänger ihop med
förvarningen och inte med samplingen.

## Varför publicering är sex gånger glesare än avläsning

Det här är den avvägning som är värd att förklara för kunden.

Skulle varje avläsning skickas blir det 8 640 ramar per enhet och dygn. Det är
inte ett problem för en enhet på ett lokalt nät, men det är ett problem så snart
kundens vision om många enheter mot en gemensam molntjänst blir verklighet.
Molnplattformar tar betalt per meddelande: på AWS IoT Core hamnar man runt
260 000 meddelanden per månad och enhet vid varje avläsning, mot 44 000 vid varje
minut. Med hundra enheter är skillnaden en kostnad man märker.

Och vinsten av att skicka oftare är liten. Ett klimat som rör sig några tiondelar
per minut är lika välbeskrivet av en punkt per minut.

Men nyckeln är att **den täta avläsningen inte går förlorad**. Valideringen kör
på alla 8 640 avläsningarna, och det är där de flesta felen fångas. Det som
skickas vidare är det senaste validerade värdet per kanal plus trenden, som är
uträknad på hela serien. Vi mäter alltså tätt för att kunna lita på data, och
skickar glest för att det räcker.

## Vad som inte skickas och varför

Just nu skickas det senaste mätvärdet per kanal, inte något statistiskt mått över
minuten. Det är ett medvetet val för prototypen: det är enklast att förklara och
lättast att jämföra mot en referensgivare.

För skarp drift vore min, max, medel och antal per kanal bättre. Då syns en
kortvarig spik som inte råkade hamna på publiceringstillfället, och
dataunderlaget blir ärligare om vad som faktiskt hände under minuten. Det ligger
i rekommendationerna inför nästa fas och är en liten ändring, eftersom
aggregeringen kan göras i `MicroHydrosApp` utan att något annat berörs.

## Hur intervallen ändras

Alla tre ligger i `firmware/src/config.h`:

```c
#define MH_SAMPLE_INTERVAL_MS   10000UL
#define MH_PUBLISH_INTERVAL_MS  60000UL
#define MH_STATUS_INTERVAL_MS   300000UL
```

Schemaläggaren läser dem vid uppstart och `Scheduler::setPeriod` finns redan, så
att ändra takt i drift är en liten sak den dagen enheten ska kunna styras utifrån.

## Svar till kunden

> **Hur ofta bör miljödata mätas?**
>
> Mät var tionde sekund, skicka varje minut.
>
> Den täta mätningen behövs inte för att beskriva klimatet, det gör en mätning i
> minuten alldeles utmärkt. Den behövs för att kunna avgöra om ett mätvärde är
> tillförlitligt. Ju tätare mätpunkterna ligger, desto mindre utrymme finns för
> ett felaktigt värde att se ut som en verklig förändring.
>
> Det är också skälet till att de två takterna inte är samma sak. Mät tätt för
> kvalitetens skull, skicka glest för kostnadens skull.
