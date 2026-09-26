# ADR-001: Val av givare

**Status:** Beslutad
**Datum:** vecka 1
**Berör:** krav FR-1 till FR-4, NFR-2

## Sammanhang

Kunden har förbjudit DHT11 och DHT22 och skriver att de var missnöjda med
"mätkvalitet, stabilitet och hur väl komponenterna passade den miljö där
produkten ska användas". De skriver också att de inte vill att komponenter väljs
bara för att de råkar finnas nära till hands.

Fyra mätpunkter ska täckas:

1. Lufttemperatur inne i odlingsutrymmet
2. Lufttemperatur utanför odlingsutrymmet
3. Temperatur i vatten eller näringslösning
4. Relativ luftfuktighet inne i odlingsutrymmet

Miljön är ett litet slutet skåp med artificiell belysning, hög luftfuktighet,
stänk från en pump och ett öppet kärl näringslösning. Det är en fuktig miljö med
snabba temperaturförändringar och en liten volym.

## Vad som faktiskt var fel med DHT22

Det är värt att vara konkret, eftersom kunden ska förstå varför den nya lösningen
är annorlunda och inte bara dyrare.

**Protokollet.** DHT22 använder ett eget enledarprotokoll där varje bit kodas som
en pulslängd på mikrosekundnivå. Det finns ingen adressering och ingen riktig
felkontroll, bara en enkel summakontroll av fyra databytes. På en mikrokontroller
som samtidigt kör WiFi kan en avbrottsrutin förskjuta tidmätningen tillräckligt
för att en bit ska läsas fel, och summakontrollen upptäcker inte alla sådana fel.
Resultatet blir enstaka felaktiga mätvärden som ser helt normala ut.

**Långtidsstabiliteten.** DHT22 bygger på en kapacitiv polymergivare utan
specificerad drift. I praktiken glider fuktmätningen flera procentenheter över
månader, särskilt i konstant hög fuktighet. Det stämmer med kundens beskrivning
att systemet fungerade vid demonstration men inte över tid.

**Upplösningen.** 0,1 °C och 0,1 %RH i utläst värde, men noggrannheten är
±0,5 °C och ±2 till 5 %RH. Det är för grovt för att se den sorts långsamma
förändring kunden vill bli varnad om, eftersom mätbruset då är i samma
storleksordning som själva trenden.

## Alternativ som jämfördes

### Luft, temperatur och fuktighet

| Krets | Temp | Fukt | Upplösning | Gränssnitt | CRC | Drift per år | Ungefärligt pris |
|---|---|---|---|---|---|---|---|
| DHT22 | ±0,5 °C | ±2–5 %RH | 0,1 | eget 1-tråds | summa | ospecificerad | 60 kr |
| AHT20 | ±0,3 °C | ±2 %RH | 0,01 | I²C | ja | ospecificerad | 30 kr |
| BME280 | ±0,5 °C | ±3 %RH | 0,01 | I²C/SPI | nej | ospecificerad | 60 kr |
| SHT31 | ±0,2 °C | ±2 %RH | 0,01 | I²C | ja | <0,25 %RH | 90 kr |
| **SHT45** | **±0,1 °C** | **±1,0 %RH** | **0,01** | **I²C** | **ja** | **<0,2 %RH, <0,02 °C** | **90 kr** |
| HDC3022 | ±0,1 °C | ±0,5 %RH | 0,01 | I²C | ja | mycket låg | 130 kr |

### Vatten

| Lösning | Noggrannhet | Upplösning | Gränssnitt | Vattentät sond | Ungefärligt pris |
|---|---|---|---|---|---|
| NTC 10k + spänningsdelare | ±1 °C okalibrerad | ADC-beroende | analog | ja | 30 kr |
| **DS18B20 i rostfri sond** | **±0,5 °C** | **0,0625 °C** | **1-Wire** | **ja, färdig** | **70 kr** |
| TMP117 | ±0,1 °C | 0,0078 °C | I²C | nej, måste gjutas in | 150 kr |
| PT1000 + MAX31865 | ±0,1 °C | hög | SPI | ja | 400 kr |

## Beslut

**Inne, luft och fukt: Sensirion SHT45 på I²C-adress 0x44.**

**Ute, luft: Sensirion SHT40-BD1B på I²C-adress 0x45.**

**Vatten: DS18B20 i vattentät rostfri sond på 1-Wire.**

## Motivering

### Varför SHT4x och inte HDC3022

HDC3022 är tekniskt bättre på fuktsidan, ±0,5 mot ±1,0 %RH. Vi valde ändå SHT45,
och skälet är inte priset utan tillgängligheten. SHT4x finns hos varje svensk
elektronikdistributör och på färdiga breakoutkort från flera tillverkare. HDC3022
finns i princip bara som lös krets hos de stora komponenthandlarna. För en första
prototyp som ska gå att bygga om och reparera väger det tyngre än tre tiondels
procentenhet fukt, särskilt eftersom ±1,0 %RH redan är en storleksordning bättre
än det kunden var missnöjd med.

Detta är dessutom en avvägning som kan omprövas senare utan att koden ändras.
Båda kretsarna sitter på I²C bakom samma `ISensor`-gränssnitt.

### Varför SHT45 inne och den enklare SHT40 ute

Inne är där besluten fattas. Det är den kanalen som trendas, som ligger till
grund för förvarningen, och som tillsammans med fuktmätningen ger daggpunkt och
VPD. Där är det värt att betala för bästa noggrannhet.

Ute är en referens. Den används för att se om skåpet blir varmare än rummet och
för att upptäcka om givarna monterats åt fel håll. Till det räcker ±0,2 °C gott.

Den verkliga vinsten med att välja två kretsar ur samma familj är dock en annan:
**det blir samma drivrutin**. En kodbas, ett CRC-fall att testa, ett datablad att
hålla reda på. Och eftersom SHT40-BD1B sitter på 0x45 medan SHT45 sitter på 0x44
kan de dela samma buss utan multiplexer.

Att utegivaren dessutom mäter fuktighet gratis är en bonus vi inte använder i
prototypen men som ligger kvar i hårdvaran. Kondens bildas där en yta är kallare
än daggpunkten, och för att räkna på kondensrisk mot skåpets ytterväggar behövs
just rummets fuktighet. Det är en trolig utveckling i nästa fas.

### Varför DS18B20 och inte något noggrannare

Vattentemperaturen är den enda kanal där givaren måste sitta nere i vätskan. Det
utesluter i praktiken allt som inte finns i en tät sond.

TMP117 är noggrannare men finns inte i vattentätt utförande. Att gjuta in den
själv i epoxi är möjligt men innebär att vi bygger en egen komponent som ingen
har provat, i en prototyp som ska vara enkel att reparera. PT1000 med MAX31865 ger
±0,1 °C men kostar fem gånger mer, kräver en SPI-krets till och är kraftigt
överdimensionerat för att mäta en näringslösning som ändrar sig några tiondelar
per timme.

Analog NTC valdes bort av ett specifikt skäl: **ESP32:ns inbyggda ADC är inte bra
nog**. Den är olinjär, har känd spridning mellan exemplar, och kräver kalibrering
per enhet för att ge något i närheten av ±1 °C. Dessutom finns ingen checksumma på
ett analogt värde, så ett glapp i kabeln blir ett tyst felaktigt mätvärde istället
för ett upptäckt fel. Hela valideringskedjan i det här projektet bygger på att vi
kan skilja på "sensorn svarade fel" och "sensorn svarade inte". Med en analog
givare försvinner den skillnaden.

DS18B20 ger ±0,5 °C, vilket räcker med marginal. Näringslösningens temperatur är
intressant i intervallet 15 till 25 °C och det som spelar roll är om den ligger
rätt och hur den rör sig, inte den absoluta tiondelen.

### Vad CRC8 faktiskt köper oss

Både SHT4x och DS18B20 skickar CRC8 över varje mätvärde. Det är den enskilt
viktigaste skillnaden mot DHT22 och skälet till att hela valideringskedjan i
[ADR-004](ADR-004-validering.md) fungerar.

Utan CRC går det inte att skilja på ett värde som är fel för att givaren mäter
fel och ett värde som är fel för att bussen störde ut. De två kräver helt olika
åtgärder: det första betyder att givaren ska bytas, det andra att kabeln ska ses
över. Med CRC blir det första lagret i valideringen gratis och exakt.

### Svar på kundens fråga om kriterievikter

Kunden bad oss avgöra vilka egenskaper som är viktigast. Så här viktade vi:

| Kriterium | Vikt | Varför |
|---|---|---|
| Långtidsstabilitet | Högst | Det var här den förra prototypen föll. En givare som driver ger fel som ingen upptäcker. |
| Felkontroll på gränssnittet | Högst | Avgör om systemet kan skilja mätfel från transportfel. |
| Lämplighet för fuktig miljö | Hög | Skåpet är fuktigt. Kondens på en givare utan skydd ger låsta värden i timmar. |
| Noggrannhet | Hög | Trendlogiken behöver att bruset är mindre än förändringen. |
| Tillgänglighet | Medel | Prototypen ska gå att bygga om. |
| Upplösning | Medel | Räcker att den är finare än noggrannheten. |
| Responstid | Låg | Förloppen i skåpet har tidskonstanter på minuter, givarna svarar på sekunder. |
| Strömförbrukning | Låg | Enheten är nätansluten, den drivs av samma uttag som lampan och pumpen. |
| Kostnad | Låg | Hela givaruppsättningen landar runt 250 kr, vilket är försumbart mot resten av en odlingsenhet. |

Responstid och strömförbrukning listas i kundbrevet men väger nästan ingenting
här, och det är värt att säga rakt ut. En givare som svarar på två sekunder är
oändligt snabb jämfört med ett vattenkärl som har en tidskonstant på en och en
halv timme. Och en enhet som ändå måste ha ström till lampan har ingen anledning
att optimera bort några hundra mikroampere.

## Konsekvenser

**Bra:**

- Noggrannheten förbättras en storleksordning mot DHT22 på både temperatur och
  fukt, och långtidsdriften är specificerad istället för okänd.
- Varje mätvärde kommer med CRC, vilket gör första valideringslagret exakt.
- Två av tre givare delar drivrutin.
- Alla tre är digitala, så inga ADC-kalibreringar per enhet.

**Mindre bra:**

- Givarkostnaden går från runt 130 kr till runt 250 kr per enhet. Med tanke på
  att en MicroHydros-enhet också innehåller lampa, pump, kärl och hölje är det
  inte den post som avgör produktkalkylen.
- Två bussar istället för en, I²C och 1-Wire. Det kostar en GPIO och ett
  motstånd till, men ger i gengäld en vattentät sond som faktiskt finns att köpa.
- SHT45 saknar det skyddande filter som HDC3022 har inbyggt. I en miljö med
  stänk bör givaren monteras så att den inte kan träffas direkt av vattenstråle.
  Det är en monteringsanvisning, inte ett komponentbyte.

## Vad som skulle ändra beslutet

Om nästa fas visar att fuktmätningen driver trots SHT45, eller om enheterna ska
stå i miljöer med direkt stänk, är HDC3022 med sitt inbyggda skydd rätt steg.
Bytet påverkar bara `mh_hw` eftersom applikationen bara ser `ISensor`.

Om kunden vill mäta pH och ledningsförmåga, vilket är naturligt för hydroponik,
tillkommer helt andra givare med helt andra problem, framför allt kalibrering som
måste göras om regelbundet. Det bör då bli ett eget beslutsunderlag.
