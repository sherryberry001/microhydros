# Individuell reflektionsrapport

Projekt 1, MicroHydros
Shehryar Hassan

---

## Min roll och arbetsinsats

Jag har genomfört projektet ensam. Rekommendationen i projektbeskrivningen är tre
studerande, och skälet till att jag avvikit från den är att jag arbetar heltid
parallellt med utbildningen. Mina arbetspass ligger inte förutsägbart nog för att
jag ska kunna vara den som andra räknar med i en grupp, och jag har genomfört
tidigare kurser på samma sätt.

Det betyder att jag har gjort allt: kravanalys, komponentresearch, arkitektur,
firmware, simuleringsmodell, gateway, tester och dokumentation. Tidsmässigt gick
ungefär 15 procent till kravanalys och research, 30 procent till firmware, 25
procent till simulering och testning, 10 procent till gatewayen och 20 procent
till dokumentation. Att testningen tog mer tid än jag planerat var inte ett
misstag i planeringen utan det som räddade projektet, vilket jag återkommer till.

Jag vill vara tydlig med vad ensamarbetet kostade, eftersom det påverkar hur
resultatet ska bedömas. En grupp får två saker gratis som jag var tvungen att
ersätta med något annat: arbetet fördelas, och någon annan ifrågasätter ens
beslut. Det första gick att lösa med planering. Det andra var svårare, och jag
återkommer till hur jag försökte lösa det.

---

## Viktiga beslut och varför de blev som de blev

### Att lägga all logik bakom gränssnitt, innan jag visste om jag behövde det

Det första verkliga beslutet var att `mh_core` inte skulle innehålla en enda rad
hårdvarukod. All applikationslogik ligger bakom fyra gränssnitt: `ISensor`,
`IClock`, `ITransport` och `ILogger`.

Jag tog det beslutet i vecka ett som en riskåtgärd. Risk R1 i projektplanen var
att givarna inte skulle finnas tillgängliga i tid, och åtgärden var att kunna
utveckla logiken oberoende av hårdvaran.

Risken blev verklighet. Hårdvaran blev aldrig tillgänglig, och prototypen är
verifierad mot en fysikalisk simuleringsmodell istället för mot monterade givare.
Att åtgärden fanns på plats innan jag visste att jag skulle behöva den är det
enskilt viktigaste skälet till att projektet ändå kunde bli klart.

Jag vill inte överdriva det. Abstraktionen löste inte allt. Jag vet fortfarande
inte om I²C fungerar med den kabellängd jag skulle ha valt, eller hur SHT45
beter sig vid verklig kondens. Det jag vann var att allt annat gick att bli klar
med, och att det som återstår är avgränsat och beskrivet istället för att vara en
oklar klump. Men den som läser den här rapporten ska veta att den sortens
begränsning inte går att abstrahera bort, bara att skjuta framåt.

### Att bygga en fysikalisk modell istället för att slumpa tal

När jag ändå skulle simulera hade det varit mycket enklare att låta sensorerna
returnera slumptal mellan 20 och 25 grader. Jag byggde istället en modell där
luften följer en första ordningens lag mot rummet plus lampans bidrag, vattnet
följer luften med en mycket längre tidskonstant, och fukten modelleras som
absolut fuktighet som räknas om till RH.

Det tog en extra dag. Det visade sig vara det bästa jag gjorde i hela projektet.

Skälet är att en modell som slumpar oberoende värden godkänner nästan vilken kod
som helst. Den har ingen inbördes struktur, så ingen validering som bygger på
struktur kan testas mot den. Med en modell som beter sig som verkligheten kunde
jag däremot testa saker som annars är omöjliga: att vattnet släpar efter luften,
att daggpunkten alltid ligger under lufttemperaturen, att RH faller när lampan
tänder trots att vattenmängden är oförändrad.

Och framför allt: modellen larmade på mig. Tre gånger.

### Att låta fysiken ligga i firmwaren och larmpolicyn i gatewayen

Ett beslut jag fick ompröva flera gånger. Slutsatsen blev att gränsen ska gå där
kompetensen sitter. Enheten är den enda som ser hela mätserien och den enda som
fungerar när nätet ligger nere, så den ska kunna svara på frågan vart
temperaturen är på väg. Men vilken marginal som är värd att väcka någon för beror
på odlingen och på vem som får notisen, och det ska gå att ändra utan att flasha
om enheter som står hemma hos kunder.

Jag tycker fortfarande att den uppdelningen är rätt, men jag noterar att jag
hittade den genom att först göra fel två gånger och inte genom att tänka ut den
i förväg.

---

## Problem som uppstod

Tre fel hittades under projektet. Alla tre hittades av att systemet fick köra,
inte av att jag läste min egen kod. Det är i sig den viktigaste iakttagelsen jag
gör i den här rapporten.

### Telemetriramen fick inte plats

Jag hade satt buffertstorleken till 640 byte på känsla. Den verkliga ramen
behöver runt 1 850. Ett enhetstest som bara kontrollerar att den största möjliga
ramen får plats avslöjade det.

Vad som hade hänt annars är obehagligt att tänka på. Funktionen returnerar noll
vid overflow, ingen ram byggs, och enheten hade fortsatt mäta och validera helt
korrekt **utan att skicka ett enda värde**. Ingenting hade loggats som ett fel.
Det är exakt den sortens tysta fel kunden beskrev från sin förra leverantör, och
jag hade byggt in det utan att märka det.

Lärdomen är enkel och jag tar med mig den: en konstant som är satt på känsla ska
ha ett test som kontrollerar den. Testet tog fem minuter att skriva.

### Trendfunktionen larmade 41 gånger på sex timmar

Det här är det jag lärde mig mest av.

Min första version drog en rät linje genom mätpunkterna och räknade ut när den
skär 30 grader. Enhetstesterna gick igenom. Första gången jag körde hela kedjan
larmade den 41 gånger i ett scenario helt utan fel, och det var odlingslampan som
tände.

Det obehagliga var att förklaringsgraden var 0,97. Statistiken sa att linjen
beskrev punkterna nästan perfekt. Den gjorde den också. Problemet var att en
linje var fel sorts kurva, och det finns inget statistiskt mått som upptäcker
det. Jag hade litat på ett mätetal utan att fråga vad det faktiskt mäter.

Min första rättelse var också fel, och det är den delen jag tycker är mest värd
att skriva ner. Jag byggde ett filter som undertryckte larm när förloppet
bromsade in. Det tog bort falsklarmen, från 41 till 5, och jag var nöjd. Sedan
provade jag det mot ett äkta fel och insåg att filtret hade tystat det också,
eftersom en skenande uppvärmning mot 38 grader också bromsar in på vägen dit.
Inbromsningen var inte det som skilde fallen åt.

Ett filter som tar bort falsklarmen genom att också ta bort de äkta larmen är
sämre än inget filter alls, och jag hade levererat det om jag bara hade mätt på
det som var trasigt.

Det som till slut fungerade var att sluta gissa och använda fysiken jag redan
kände till. Luften i ett skåp är ett första ordningens system, vilket betyder att
lutningen bär information om var förloppet är på väg. Med lutningen och
medelvärdet i fönstrets två halvor går jämvikten att räkna ut i ett slutet
uttryck. Ligger den under gränsen nås gränsen aldrig, hur brant det än lutar just
nu.

41 falsklarm blev noll, och förvarningen blev sju minuter.

### Sensorbruset slog i derivatagränsen

Det tredje felet var subtilare. Jag hade räknat fram att några liter vatten inte
kan ändra temperatur snabbare än ungefär 0,3 grader per minut, och satte gränsen
till 1,0 med god marginal. Uträkningen var korrekt.

Men en DS18B20 brusar 0,05 grader och kvantiserar i steg om 0,0625. Två
avläsningar tio sekunder isär skiljer sig därför typiskt 0,07 grader av rena
mätskäl, vilket omräknat blir 0,42 grader per minut. Ett par standardavvikelser
räckte för att slå i taket, och 3,6 procent av alla vattenmätningar underkändes i
helt felfri drift.

Jag hade härlett en gräns ur fysiken och glömt att den givare som mäter fysiken
har en egen osäkerhet. Rättelsen var att jämföra absoluta steg istället för
hastigheter och lägga till brusnivån som en konstant. 3,6 procent blev 0,00.

Det jag noterar är att enhetstesterna inte hittade det här. Ett av dem
kontrollerade att andelen tveksamma mätvärden låg under en procent, och eftersom
felet bara drabbade en av fyra kanaler hamnade totalen strax under tröskeln.
Felet hittades av en databasfråga mot verklig data efter en körning. Ett test med
en tröskel kan dölja ett problem som en fråga mot data visar direkt.

---

## Hur arbetssättet fungerade

Jag använde en enkel kanban med de tre kolumnerna att göra, pågående och klart,
och en backlog prioriterad enligt MoSCoW. Ingen sprintplanering, eftersom det
inte finns något att synkronisera när man är ensam, och inga stand-ups av samma
skäl.

Det som faktiskt gjorde skillnad var MoSCoW-prioriteringen. Den blev min
ersättning för en produktägare som säger nej. När tiden blev knapp i vecka tre,
efter att de tre felen dykt upp, kunde jag flytta aggregering av min, max och
medel till nästa fas utan att grubbla. Det stod under Could have, och kundens
eget brev sa att en mindre lösning som fungerar stabilt är mer värd än en stor
där mycket fungerar delvis.

Att skriva beslutsunderlagen löpande och inte i efterhand var det andra som
fungerade. I en grupp motiverar man sina val muntligt för någon som invänder.
Ensam blir det lätt att ett val bara blir till utan att man märker att man har
valt. Att tvinga sig att skriva ner vilka alternativ som fanns och varför de
valdes bort är den närmaste ersättningen jag hittade, och det avslöjade minst ett
val som jag inte kunde motivera när jag försökte formulera det.

Men jag ska vara ärlig med att det inte riktigt räckte. Ett beslutsunderlag jag
skriver själv granskas av mig, och jag är inte särskilt kritisk mot mina egna
resonemang. De tre felen hittades inte av mina ADR-dokument. De hittades av att
systemet kördes och mätte på sig självt.

Det är min viktigaste slutsats om ensamarbete: **en testrigg som kan provocera
fram fel på begäran är den bästa ersättningen för en kollega som ifrågasätter
dig.** Den har inga åsikter om hur smart koden är, och den bryr sig inte om att
jag redan bestämt mig. Den kör och redovisar.

---

## Vad jag skulle göra annorlunda

**Köra hela kedjan mycket tidigare.** Jag byggde firmware, sedan simulering,
sedan gateway, och körde ihop alltihop först i vecka tre. Alla tre felen dök upp
inom de första timmarna av den körningen. Hade jag kopplat ihop en grov version
av hela kedjan redan i slutet av vecka ett hade jag haft två veckor på mig att
hitta dem istället för några dagar, och vecka tre hade inte blivit så pressad.

Det här är det jag ångrar mest, och jag tror att det är ett vanligt misstag: det
känns produktivt att göra varje del färdig innan man kopplar ihop dem, men det
skjuter upp den dag då man får veta om det fungerar.

**Skaffa hårdvara tidigare, eller bestämt mig tidigare för att inte ha den.** Jag
hamnade i ett mellanläge där jag räknade med hårdvara ett tag till innan det blev
tydligt att den inte skulle komma. Hade jag bestämt mig tidigare kunde jag lagt
den tid jag ägnade åt att vänta på att i stället bygga ut simuleringsmodellen
med kondens och egenuppvärmning, vilket hade täckt in två av de saker som nu står
oprövade.

**Testat filtret mot båda fallen från början.** När jag byggde inbromsningsfiltret
mätte jag bara på det som var trasigt, alltså falsklarmen. Att också mäta på det
som skulle fungera är en vana jag inte hade då och har nu. Det är samma sak som
principen jag sedan skrev in i teststrategin: för varje test som visar att ett fel
upptäcks ska det finnas ett motprov.

**Varit tydligare i planen med vad som händer om hårdvaran uteblir.** Jag hade
risken i riskanalysen och jag hade en åtgärd, men jag hade inte skrivit ner vad
konsekvensen skulle bli om risken slog in. Det gjorde att jag dröjde med att
skriva avsnitt 10.1 i den tekniska dokumentationen, trots att det var uppenbart
för mig i flera dagar att det var där projektet skulle landa.

---

## Passade arbetssättet projektets förutsättningar?

Delvis.

Det som passade bra var den lätta processen. En enmansgrupp som inför
sprintceremonier utför ritualer utan innehåll, och jag hade hellre lagt de
timmarna på tester. Kanban med en prioriterad backlog gav precis så mycket
struktur som behövdes för att veta vad som var nästa sak och vad som fick falla
bort.

Det som passade dåligt var att jag inte hade någon som ifrågasatte mig. Två av
de tre felen berodde på att jag hade fattat ett beslut som kändes självklart, och
ett självklart beslut är just den sorten som ingen granskar. Att dra en rät linje
genom mätpunkterna kändes självklart. Att en gräns härledd ur fysiken är rätt
gräns kändes självklart.

Simuleringsriggen tog den rollen till slut, men den kom sent. Om jag gör ett
soloprojekt igen bygger jag riggen först, innan jag bygger det den ska testa.

Att jag arbetar heltid parallellt påverkade främst hur arbetet kunde fördelas
över dygnet, inte kvaliteten. Men det gjorde att marginalen var tunn. Hade de
tre felen dykt upp i vecka fyra istället för vecka tre hade jag fått leverera
något sämre.

---

## Vad jag tar med mig

**En bra statistisk anpassning är inte samma sak som en riktig modell.** r² på
0,97 sa att linjen beskrev punkterna väl, och linjen var ändå fel sorts kurva.
Jag kommer att fråga mig en gång till vad ett mätetal faktiskt mäter innan jag
litar på det.

**Ett filter måste prövas mot både falsklarmet och det äkta larmet.** Att mäta
bara på det som är trasigt gör att en försämring kan se ut som en förbättring.

**En gräns härledd ur fysiken måste ta hänsyn till mätosäkerheten hos den som
mäter fysiken.** Uträkningen kan vara korrekt och ändå inte vara hela sanningen.

**Att hålla logiken fri från hårdvaruberoenden är en riskåtgärd, inte en
estetisk preferens.** Det var det beslutet som gjorde att projektet kunde bli
klart när hårdvaran uteblev. Och det var det som gjorde att jag kunde köra sju
simulerade dygn på under en sekund, vilket i sin tur var det som gjorde att
testning blev något jag gjorde ofta istället för något jag gjorde till slut.

**Koppla ihop hela kedjan tidigt, även om delarna är grova.** Alla fel jag
hittade hittades där. Ingen av dem hittades av att jag läste koden.

**Skriv ner begränsningarna innan någon frågar.** Det var obekvämt att skriva
avsnittet om att prototypen inte är verifierad mot fysisk hårdvara, och det var
uppenbart rätt att göra. Ett arbete som redovisar vad det inte vet är mer värt än
ett som låter som om det vet allt, och kundens eget brev handlar just om en
leverantör som visade upp något som såg färdigt ut.
