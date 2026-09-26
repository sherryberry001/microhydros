# ADR-006: Förvarning genom modellidentifiering

**Status:** Beslutad efter tre omtag
**Datum:** vecka 3
**Berör:** krav FR-9
**Svarar på kundens önskemål:** "varna användaren **innan** temperaturen når en nivå som riskerar att skada växterna"

## Sammanhang

Kunden skriver:

> Ett framtida system skulle exempelvis kunna upptäcka att temperaturen
> successivt ökar och varna användaren **innan** temperaturen når en nivå som
> riskerar att skada växterna.

De säger också att vi inte behöver utveckla hela analysfunktionen i den första
prototypen, men att systemets utformning bör ta hänsyn till att historiska
mätdata blir viktiga.

Vi valde att bygga en enkel version av förvarningen ändå, av två skäl. Dels är
det den funktion som tydligast skiljer ett mätsystem från ett övervakningssystem,
och därmed den som bäst visar vad konceptet kan bli. Dels avslöjar den om
datamodellen duger, eftersom den kräver en serie och inte ett värde.

Det här dokumentet beskriver tre försök. De två första var fel, och båda felen
upptäcktes genom att köra systemet och titta på vad det faktiskt larmade om.
Att beskriva dem är inte självkritik för sakens skull: vägen fram är själva
motiveringen till varför den slutliga lösningen ser ut som den gör.

## Försök 1: linjär extrapolering

Den självklara första versionen. Anpassa en rät linje över ett glidande fönster
med minstakvadratmetoden, och räkna ut när linjen skär gränsen på 30 °C.

```
minuter_kvar = (gräns − nuvarande) / lutning × 60
```

Enhetstesterna gick igenom. En serie som stiger 6 °C per timme från 24 °C gav
korrekt 60 minuter till 30 °C.

### Vad som gick fel

Vid första hela körningen genom kedjan larmade systemet **41 gånger på sex
simulerade timmar**, i ett scenario helt utan fel.

Loggen visade vad som hände:

```
FORVARNING: air_in når 30.0 C om 11 min (46.90 C/h, r2 0.97)
FORVARNING: air_in når 30.0 C om 11 min (42.17 C/h, r2 0.99)
FORVARNING: air_in når 30.0 C om 10 min (41.80 C/h, r2 1.00)
```

Det var odlingslampan som tände.

När lampan tänder strävar lufttemperaturen mot en ny jämvikt enligt en
exponentialfunktion. Precis i början av det förloppet är lutningen brant, runt
47 °C per timme. En rät linje dragen genom de första minuterna pekar rakt genom
30 °C. I verkligheten planar kurvan ut på 27,5 °C och gränsen nås aldrig.

Systemet larmade alltså varje morgon, och förklaringsgraden var 0,97, alltså
nästan perfekt anpassning. Statistiken var rätt. Modellen var fel.

**Lärdomen:** hög förklaringsgrad säger att linjen beskriver punkterna väl. Den
säger ingenting om att en linje är rätt sorts kurva.

## Försök 2: undertryck larm när förloppet bromsar in

Idén var att mäta inbromsning. Dela fönstret i två halvor, jämför lutningarna,
och undertryck larmet om den senare halvan är märkbart flackare.

Det tog bort falsklarmen. Från 41 ner till 5, och de fem kvarvarande kom alla
från de första minuterna efter start, innan fönstret hunnit se tillräckligt av
förloppet för att kurvaturen skulle synas.

### Vad som gick fel

Filtret hade tystat det verkliga felet också.

En skenande uppvärmning, till exempel när kylningen slutar fungera, följer
**samma sorts kurva**. Luften strävar mot en ny jämvikt, bara att jämvikten nu
ligger på 33 °C istället för 27,5 °C. Förloppet bromsar in på exakt samma sätt.
Filtret kunde inte skilja dem åt, eftersom inbromsningen inte är det som skiljer
dem.

Det fanns dessutom ett andra fel som blev synligt i samma körning. Systemet
larmade med **negativ** lutning:

```
FORVARNING: air_in når 30.0 C om 59 min (-1.22 C/h, r2 0.86)
```

Temperaturen var på väg **ner** genom 30 °C efter att problemet klingat av.
Matematiskt stämde uträkningen, men semantiskt är det återhämtningen och inte ett
nytt problem.

**Lärdomen:** ett filter som tar bort falsklarm genom att också ta bort de äkta
larmen är sämre än inget filter alls. Och det gick bara att se genom att pröva
filtret mot båda fallen, inte bara mot det som var trasigt.

## Försök 3: identifiera systemet

Lösningen var att sluta gissa och istället använda den fysik vi redan kände till.

Luften i ett odlingsskåp är ett första ordningens system. Det betyder att
förändringstakten är proportionell mot hur långt det är kvar till jämvikten:

```
dy/dt = (A − y) / τ
```

där `A` är jämviktstemperaturen och `τ` tidskonstanten.

Den ekvationen säger att **lutningen bär information om var förloppet är på väg**.
Mäter man lutningen och medelvärdet i fönstrets två halvor får man två ekvationer
och två obekanta, som går att lösa ut i slutna uttryck:

```
τ = (y_sen − y_tidig) / (s_tidig − s_sen)
A = y_sen + s_sen · τ
```

Då blir frågan enkel och fysikaliskt riktig:

- Ligger den skattade jämvikten **över** gränsen kommer vi dit, och tiden räknas
  ur exponentialen: `t = −τ · ln((A − gräns) / (A − nu))`
- Ligger den **under** gränsen nås den aldrig, hur brant det än lutar just nu.

Det här skiljer korrekt mellan de två fallen, eftersom skillnaden mellan dem
just är var de är på väg och ingenting annat.

### Tre detaljer som krävdes för att det skulle fungera

**Nästan linjära förlopp.** Om lutningen är i stort sett oförändrad mellan
halvorna går `τ` mot oändligheten och skattningen blir meningslös. Då faller
koden tillbaka på linjär extrapolering, vilket är det konservativa valet: inget
tyder på att förloppet planar ut, alltså ska vi varna.

**Regimskiften.** Om lutningen har **ökat** mellan halvorna har något just hänt.
Fönstret straddlar då två olika förlopp, till exempel lugnt mörker följt av att
lampan tänder, och en första ordningens modell passar inte på två förlopp
samtidigt. Koden försöker då om på enbart fönstrets senare halva, som med stor
sannolikhet ligger helt inom det nya förloppet.

**Fönsterlängden.** Det här var det sista felet och det minst uppenbara.

Första versionen hade ett fönster på 15 minuter. Skåpets tidskonstant är ungefär
8 minuter. När något gick sönder innehöll fönstret fortfarande mest data från
innan felet, modellen gick inte att identifiera, och förvarningen krympte till en
minut.

Tumregeln är att fönstret ska vara ungefär lika långt som eller kortare än det
förlopp det ska beskriva. Fönstret kortades till **36 avläsningar, alltså sex
minuter**. Förvarningstiden gick från 1 minut till 7.

Att korta fönstret kostar precision i lutningen, men marginalen är god. Med
SHT45:s brus på 0,04 °C och 36 punkter över sex minuter blir medelfelet i
lutningen ungefär 0,3 °C per timme, vilket ska jämföras med de tiotals grader per
timme ett verkligt fel ger. Bruset är alltså inte det som begränsar.

## Beslut

**Firmwaren identifierar en första ordningens modell ur trendfönstret och
rapporterar fakta. Gatewayen sätter larmpolicyn.**

Firmwaren skickar med varje ram:

| Fält | Betydelse |
|---|---|
| `direction` | rising, falling, stable, unknown |
| `slope_per_hour` | lutning |
| `r_squared` | hur väl en linje beskriver punkterna |
| `projected_equilibrium_c` | vad förloppet är på väg mot, eller `null` |
| `tau_minutes` | skattad tidskonstant |
| `minutes_to_limit` | tid kvar till gränsen, eller `null` |
| `limit_exceeded` | gränsen är redan passerad |

Gatewayen larmar när `minutes_to_limit` ligger inom en timme, fönstret innehåller
minst 30 punkter och förklaringsgraden är minst 0,80. Ett överskridet gränsvärde
rapporteras som ett eget, annorlunda meddelande.

### Varför fysiken i firmwaren och policyn i gatewayen

Gränsen går där kompetensen sitter.

Firmwaren är den enda som ser hela serien med tio sekunders upplösning, och den
enda som fungerar när nätet ligger nere. Den ska kunna svara på frågan "vart är
det här på väg" oavsett uppkoppling.

Vilken marginal som är värd att väcka någon för beror däremot på odlingen och på
vem som får notisen. En restaurang med sallad vill ha en halvtimmes framförhållning,
ett forskningsskåp kanske fem minuter. Det ska gå att ändra utan att flasha om
enheter som står hemma hos kunder.

## Resultat

Från samma fyra scenarier, sex simulerade timmar vardera:

| Scenario | Förvarningar | Kommentar |
|---|---|---|
| normal | 0 | rätt, klimatet är stabilt |
| faults | 0 | rätt, givarfel ska inte ge klimatlarm |
| outage | 0 | rätt |
| heatwave | 6, följt av ett överskridande | rätt |

Den första förvarningen kom **7 minuter innan** gränsen passerades, med den
skattade jämvikten på 32,5 °C mot det verkliga värdet 33,5 °C.

```
FORVARNING: air_in når 30.0 C om 7 min (39.61 C/h, på väg mot 32.5 C, r2 0.80)
FORVARNING: air_in når 30.0 C om 6 min (55.86 C/h, på väg mot 32.2 C, r2 0.93)
FORVARNING: air_in når 30.0 C om 3 min (66.00 C/h, på väg mot 42.9 C, r2 0.99)
...
GRANS OVERSKRIDEN: air_in ligger över 30.0 C just nu
```

Jämför med 41 falsklarm och noll äkta i första versionen.

## Kända begränsningar

**Bara lufttemperaturen inne trendas i firmwaren.** Det är den kanal kunden
faktiskt vill bli varnad om. Övriga kanaler har all sin historik i gatewayen där
det finns plats att trenda dem.

**Modellen antar ett första ordningens förlopp.** Det stämmer för luft i ett
slutet skåp. Det stämmer sämre om någon öppnar luckan mitt i, vilket ser ut som
ett regimskifte. Koden hanterar det genom att försöka om på senare halvan, men
under ett par minuter efter en sådan störning är skattningen osäker.

**Enstaka skattningar kan slå fel.** I resultatet ovan syns ett värde på 42,9 °C
mitt bland 32-värdena. Det är brus i lutningsskattningen som slår igenom i
divisionen. Larmet blev ändå rätt eftersom alla skattningar låg över gränsen. Att
filtrera jämviktsskattningen över flera ramar vore en billig förbättring.

**Ingen hysteres.** Ett klimat som pendlar runt gränsen ger upprepade larm.
Gatewayen undertrycker upprepade identiska överskridanden, men det är en grov
lösning. Riktig hysteres, till exempel att larmet måste återställas under 28 °C
innan det kan lösa ut igen, hör till nästa fas.

**Gränsen på 30 °C är en kompilerad konstant.** Den borde vara inställbar per
odling, eftersom basilika och sallad har olika tålighet.

## Vad som togs med från det här

Tre saker som gäller mer allmänt än det här projektet:

1. **En bra statistisk anpassning är inte samma sak som en riktig modell.** r² på
   0,97 sa att linjen beskrev punkterna väl, och linjen var ändå fel sorts kurva.

2. **Ett filter måste prövas mot både falsklarmet och det äkta larmet.** Försök 2
   såg utmärkt ut mätt på det som var trasigt, och var sämre än ingenting mätt på
   helheten.

3. **Fönsterlängden är en teknisk parameter med fysikalisk innebörd.** Femton
   minuter valdes för att det kändes lagom. Sex minuter valdes för att skåpets
   tidskonstant är åtta. Skillnaden i resultat var sju gånger längre förvarning.
