# ADR-004: Validering och kvalitetsflaggor

**Status:** Beslutad
**Datum:** vecka 2, justerad vecka 3
**Berör:** krav FR-6, NFR-2
**Svarar på kundens fråga:** "Hur kan systemet upptäcka felaktiga eller orimliga mätvärden?"

## Sammanhang

Kunden skriver att deras förra prototyp "fungerade vid demonstration, men visade
sig vara betydligt mindre tillförlitlig när systemet testades under längre tid".

Den meningen är projektets viktigaste krav, även om den inte står i kravlistan.
Den beskriver ett system som gav mätvärden hela tiden, men där mätvärdena
ibland var fel och ingen kunde se vilka.

## Varför det är svårt

Den intuitiva bilden är att en trasig givare slutar svara. Så går det nästan
aldrig till. En trasig givare fortsätter svara, men fel, och felet ser ut som ett
helt vanligt mätvärde.

Fyra konkreta exempel, alla dokumenterade i respektive datablad eller allmänt
kända:

| Vad som händer fysiskt | Vad givaren rapporterar | Ser det fel ut? |
|---|---|---|
| DS18B20 tappar kontakt eller saknar pullup | **exakt +85,0 °C** | Nej. Ett helt normalt tal inom mätområdet. |
| Kondens sätter sig i en fuktgivare | **100 %RH i timmar** | Nej. Full mättnad är ett giltigt tillstånd. |
| I²C-bussen hänger sig | **samma register om och om igen** | Nej. Värdet är rimligt och derivatan är noll. |
| Polymergivare åldras | **glider några procentenheter över månader** | Nej. Varje enskild avläsning är rimlig. |

Inget av de här felen fångas av ett rimlighetstest som bara kollar om värdet
ligger i ett intervall. +85,0 °C ligger innanfor DS18B20:s mätområde. 100 %RH
ligger innanför fuktgivarens. Ett fastnat värde ligger per definition där det
brukar ligga.

Det är därför valideringen behöver flera lager som ser olika saker.

## Beslut

### Fem lager, där varje lager fångar något de andra inte ser

| Lager | Vad som kontrolleras | Vilket fel det fångar | Utfall |
|---|---|---|---|
| **L1** | CRC8 och bussrespons | störd kabel, glapp, kretsen borta | `bad` |
| **L2** | mot databladets mätområde | vi läser inte givaren, vi läser brus | `bad` |
| **L3** | mot vad som är rimligt i en odling | fysiskt möjligt men inte här | `suspect` |
| **L4** | hur snabbt värdet ändrar sig | hopp som fysiken inte tillåter | `suspect` |
| **L5** | identiskt värde för länge | hängd buss, låst givare | `suspect` |

Utöver dessa finns **korskontroller** som jämför kanaler mot varandra. De är
värdefulla just för att de fångar fel där varje enskild kanal ser helt frisk ut.

| Korskontroll | Fysisk grund | Vilket fel det fångar |
|---|---|---|
| Daggpunkt ≤ lufttemperatur | definitionsmässigt omöjligt annars | temperatur- eller fuktkanalen ljuger |
| Vatten inom 15 °C från luften | termisk koppling i ett litet skåp | sonden har lossnat ur kärlet |
| Inne ≥ ute minus 5 °C | lampa och pump tillför effekt | de två luftgivarna är monterade åt fel håll |

Den sista är värd att nämna särskilt. Två förväxlade givare ger två fullständigt
normala mätvärden och upptäcks aldrig av någon kontroll som tittar på en kanal i
taget. Men monteringsfelet är lätt att göra, för de två kablarna ser likadana ut.

### Tre kvalitetsnivåer, inte två

| Nivå | Betydelse | Värde | Vad mottagaren gör |
|---|---|---|---|
| `good` | passerade allt | talet | använd |
| `suspect` | fysiskt möjligt, men något är konstigt | talet | lagra och rita, men larma inte och trenda inte |
| `bad` | inte användbart | `null` | rita ett hål i kurvan |

Mellansteget `suspect` är viktigt och var inte självklart. Skälet är att ett
värde kan vara tveksamt utan att vara fel. En näringslösning på 40 °C är orimlig
i en odling, men om någon har ställt skåpet i solen är den sann. Kastar vi bort
det värdet döljer vi det verkliga problemet. Markerar vi det syns både värdet och
att systemet reagerade på det.

### Underkända värden kastas aldrig bort

Det här är kanske det viktigaste beslutet i hela dokumentet.

Ett mätvärde som inte håller måttet skickas ändå vidare, med sin kvalitetsflagga
och en orsakskod. Gatewayen lagrar det. Tre konsekvenser:

1. **En graf kan rita ett hål** där data saknas, istället för att dra en rak
   linje över ett fel som ingen upptäckte. Den raka linjen är precis hur den förra
   prototypen kunde se frisk ut samtidigt som den inte var det.
2. **Felfrekvensen går att räkna på i efterhand.** Vyn `v_sensor_health` visar
   per givare hur många mätvärden som godkändes, var tveksamma och underkändes.
   Det är underlaget för att svara på frågan "går det att lita på den här
   givaren", som kunden inte kunde få svar på förra gången.
3. **Orsakskoden gör felsökning på distans möjlig.** Skillnaden mellan
   `checksum_failed` och `no_response` är skillnaden mellan "se över kabeln" och
   "givaren är borta". Utan orsakskod måste någon åka dit.

Ett `bad`-värde får `null` som tal och aldrig `0`. En nolla ser ut som en riktig
mätning nere vid nollstrecket.

### Rollfördelning mellan enhet och gateway

| Vem | Avgör | Varför just där |
|---|---|---|
| Enheten | om ett **mätvärde** är rimligt | bara den ser hela serien med tio sekunders upplösning, och bara den fungerar när nätet ligger nere |
| Gatewayen | om en **ram** är begriplig | schemaversion, obligatoriska fält, dubbletter, hål i löpnummerserien |

Gatewayen litar alltså inte blint på enheten, men den gör inte heller om enhetens
arbete. Den kontrollerar det den är ensam om att kunna se.

## Hur gränsvärdena är satta

Siffrorna är inte gissade. Varje gräns har en härkomst.

### Mätområde (L2): från databladen

```
SHT4x       -40 .. 125 °C,  0 .. 100 %RH
DS18B20     -55 .. 125 °C
```

Ett värde utanför detta betyder att vi inte läser givaren utan brus.

### Rimligt område (L3): från användningsfallet

| Kanal | Gräns | Resonemang |
|---|---|---|
| Luft inne | 0 till 60 °C | Fryser det inne i skåpet är odlingen förlorad ändå. Över 60 °C är det brand, inte odlingsklimat. |
| Luft ute | −20 till 50 °C | Enheten kan stå i ett ouppvärmt förråd. |
| Vatten | 0 till 45 °C | Fryst eller nära kokande näringslösning. |
| Fukt inne | 15 till 100 %RH | Ett slutet skåp med ett öppet kärl vatten och transpirerande växter kommer aldrig under 15 %RH. Gör det det är givaren trasig. |

### Derivatagräns (L4): från termisk tröghet

Vattnet är det tydligaste exemplet. Några liter näringslösning har en
värmekapacitet i storleksordningen 10 kJ per kelvin. Lampan och pumpen tillför
tillsammans under 50 W till lösningen. Det ger

```
dT/dt  =  50 W / 10 000 J/K  =  0,005 K/s  =  0,3 °C per minut
```

Vi satte gränsen till 1,0 °C per minut, alltså mer än tre gånger det fysiskt
möjliga. Den generositeten är avsiktlig: gränsen ska inte behöva stämma exakt för
varje tänkbar skåpstorlek. Och den fångar ändå det verkliga felet med enorm
marginal, eftersom en lossnad sond hoppar 60 grader på tio sekunder.

### Brustillägg: en rättelse som kom ur körningar

Den första versionen jämförde ren hastighet mot gränsen. Det visade sig vara
fel, och felet syntes först när systemet fick köra.

En DS18B20 brusar omkring 0,05 °C och kvantiserar i steg om 0,0625 °C. Två
avläsningar tio sekunder isär skiljer sig därför typiskt 0,07 °C av rena mätskäl,
vilket omräknat blir 0,42 °C per minut. Med en gräns på 1,0 räckte det med ett
par standardavvikelser för att slå i taket. I den första hela körningen
underkändes **3,6 procent av alla vattenmätningar i helt felfri drift**.

Det är precis så ett larm blir värdelöst. En användare som får falsklarm var
halvtimme slutar titta på dem.

Rättelsen var att mäta absoluta steg istället för hastigheter, och lägga till
givarens egen mätosäkerhet som en konstant:

```
tillåtet steg  =  max_rate_per_min × dt_minuter  +  rate_noise_allowance
```

Tillägget sätts till ungefär fyra gånger standardavvikelsen för skillnaden mellan
två avläsningar:

| Kanal | Givarens brus | Tillägg | Tillåtet steg vid 10 s |
|---|---|---|---|
| Luft inne | 0,04 °C | 0,25 °C | 1,25 °C |
| Luft ute | 0,08 °C | 0,40 °C | 0,73 °C |
| Vatten | 0,05 °C + kvantisering | 0,30 °C | 0,47 °C |
| Fukt inne | 0,35 %RH | 1,5 %RH | 4,8 %RH |

Efter rättelsen: **0,00 procent underkända i felfri drift**, och den verkliga
feltypen har fortfarande över hundra gånger marginalen kvar. Båda siffrorna är
låsta med regressionstester, se testrapportens fynd F-3.

Lärdomen är värd att skriva ut, eftersom den gäller allmänt: **en gräns som
härleds ur fysiken måste ändå ta hänsyn till mätosäkerheten hos den givare som
mäter fysiken.** Räkningen på 0,3 °C per minut var korrekt. Den var bara inte
hela sanningen.

### Låst värde (L5): 30 avläsningar

Trettio identiska avläsningar i rad motsvarar fem minuter vid tio sekunders
intervall. Det är kort nog för att upptäcka en hängd buss innan någon saknar
datan, och långt nog för att ett verkligt stillastående klimat inte ska larma,
eftersom en frisk givare alltid brusar några hundradelar.

Toleransen för "identisk" är satt finare än givarens upplösning, så det verkligen
är bitidentiska värden som räknas och inte ett stillastående klimat.

## Två detaljer som är lätta att göra fel

**En spik får inte flytta referensen.** Derivatakontrollen jämför mot det senast
*godkända* värdet, inte mot den senaste avläsningen. Annars skulle en ensam spik
till 85 °C göra att nästa korrekta värde på 21 °C ser ut som ett lika stort hopp
åt andra hållet, och systemet skulle underkänna två värden istället för ett. Det
finns ett test för just detta.

**Ett härlett värde kan aldrig vara bättre än sina indata.** Daggpunkt och VPD
ärver den sämsta kvaliteten av lufttemperatur och fuktighet, med orsakskoden
`derived_from_suspect` så att man ser att felet ligger i indata och inte i
beräkningen.

## Konsekvenser

**Bra:**

- Alla fyra injicerade felmoder upptäcks med rätt orsakskod, se testrapporten.
- Noll falsklarm i felfri drift efter brustillägget.
- Orsakskoden gör fjärrfelsökning möjlig.
- Historiken blir ärlig: hål syns som hål.

**Mindre bra:**

- Gränsvärdena är satta för ett skåp av en viss storlek. En större MicroHydros
  har andra tidskonstanter och behöver andra gränser. De ligger samlade i
  `ChannelLimits` men är kompilerade konstanter, inte konfiguration.
- Långsam drift, den fjärde feltypen i tabellen ovan, fångas **inte**. En givare
  som glider 0,1 °C per månad passerar varje lager. Att upptäcka det kräver
  antingen en referensgivare eller statistik över månader, och det hör hemma i
  molnet där historiken finns. Det är listat som en känd begränsning.
- Fem lager kostar kod och test. Valideringen är den enskilt mest testade delen
  av projektet, med 17 enhetstester, och det är rätt fördelning eftersom det var
  här den förra prototypen föll.

## Svar till kunden

> **Hur kan systemet upptäcka felaktiga eller orimliga mätvärden?**
>
> Genom att fråga fem olika saker om varje mätvärde, eftersom ingen enskild fråga
> räcker. Är checksumman rätt? Ligger värdet inom vad givaren kan mäta? Är det
> rimligt för en odling? Kan det ha ändrat sig så snabbt? Har det fastnat?
>
> Och sedan genom att jämföra givarna mot varandra. Två givare kan var för sig
> ge fullt rimliga värden och ändå vara omöjliga tillsammans. Det är så vi
> upptäcker att vattensonden har lossnat ur kärlet eller att luftgivarna är
> monterade åt fel håll.
>
> Det som inte upptäcks är långsam drift. En givare som glider en tiondels grad
> i månaden ser frisk ut varje enskild dag. Det kräver antingen en referensgivare
> eller flera månaders historik att jämföra mot, och det bör byggas i molntjänsten
> där historiken ändå ska ligga.
