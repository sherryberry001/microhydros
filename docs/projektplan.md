# Projektplan, MicroHydros

Projekt 1, IoT- och embeddedutvecklare
Projektperiod 31 augusti till 25 september 2026

---

## 1. Sammanfattning av uppdraget

HydroGreen Fingers AB utvecklar MicroHydros, en kompakt hydroponisk odlingsenhet
som ska kunna placeras i bostäder, restauranger, skolor och kontor. Enheten
odlar under artificiell belysning i en liten volym, vilket gör att temperatur och
luftfuktighet kan förändras snabbt.

Uppdraget är att ta fram en första teknisk prototyp som samlar in miljödata från
odlingen och visar att data kan kommuniceras till ett externt system.

Kunden har ett tidigare misslyckat försök bakom sig. Deras beskrivning av vad som
gick fel är den viktigaste meningen i hela underlaget:

> Resultatet fungerade vid demonstration, men visade sig vara betydligt mindre
> tillförlitligt när systemet testades under längre tid.

Det är inte en beställning av fler funktioner. Det är en beställning av
tillförlitlighet, och det har fått styra hela projektet. DHT11 och DHT22 är
uttryckligen förbjudna.

## 2. Projektets mål

### Huvudmål

Leverera en prototyp som mäter fyra miljöparametrar återkommande, bedömer om
varje mätvärde går att lita på, och skickar resultatet till ett externt system
som lagrar historiken.

### Delmål

| Nr | Mål | Hur det mäts |
|---|---|---|
| M1 | Fyra mätpunkter läses in återkommande | mätvärden syns i databasen med tidsstämplar |
| M2 | Varje mätvärde bär en kvalitetsbedömning | `quality` och `reason` finns i varje mätvärde |
| M3 | Felaktiga mätvärden upptäcks och markeras | injicerade fel ger rätt orsakskod i testkörning |
| M4 | Friska givare ger inga falsklarm | noll underkända mätvärden i felfri drift |
| M5 | Mätdata når ett externt system | ramar lagras i SQLite via MQTT |
| M6 | Dataförlust vid nätavbrott undviks och blir synlig | kö töms efter avbrott, hål syns i löpnummerserien |
| M7 | Systemet varnar innan gränsvärde nås | förvarning före överskridande i testscenario |
| M8 | Tekniska val är motiverade och nedskrivna | beslutsunderlag för varje större val |

### Vad som uttryckligen inte är mål

Att bygga en färdig kommersiell produkt. Kunden skriver att en mindre lösning
som fungerar stabilt är betydligt mer värdefull än en stor där många funktioner
fungerar delvis, och uppgiften säger att extra funktionalitet inte ger högre
betyg. Det har hållits.

## 3. Avgränsningar

| Utanför omfattningen | Varför |
|---|---|
| Färdig molnplattform | Kunden har inte valt plattform. Prototypen ska visa att systemet **kan** kommunicera, inte låsa fast ett val. |
| Mobilapp | Inte obligatoriskt. Skulle ta tid från tillförlitligheten. |
| Avancerat webbgränssnitt | En minimal panel byggs för att demonstrationen ska ha något att peka på. |
| Maskininlärning | Det finns ingen historik att träna på. En fysikalisk modell är både enklare och bättre här. |
| Automatisk bevattning och belysningsstyrning | Reglering kräver att mätningen först är tillförlitlig. Fel ordning att bygga nu. |
| Färdig fysisk produktdesign | Prototypen byggs på kopplingsdäck. |
| Kalibrering mot spårbar referens | Kräver utrustning som inte finns. Givarna är fabrikskalibrerade. |

### En avgränsning som kom till under projektet

**Verifiering mot fysisk hårdvara.** Prototypen är utvecklad och verifierad mot
en fysikalisk simuleringsmodell av odlingsskåpet, inte mot monterade givare.
Firmwaren kompilerar för ESP32 och drivrutinerna är skrivna mot databladen, men
I²C-timing, kabellängder och verklig kondens är inte provade.

Det här är dokumenterat som den främsta begränsningen i
[teknisk-dokumentation.md avsnitt 10.1](teknisk-dokumentation.md#101-prototypen-är-verifierad-i-simulering-inte-på-fysisk-hårdvara),
och hårdvaruverifiering är den första punkten i rekommendationerna för nästa fas.

## 4. Identifierade krav

Fullständig kravlista med spårbarhet till tester finns i [krav.md](krav.md).
Sammanfattning:

| Kategori | Antal | Uppfyllda |
|---|---|---|
| Funktionella krav (FR) | 12 | 12 |
| Icke-funktionella krav (NFR) | 8 | 7, ett delvis |

Det delvis uppfyllda är NFR-6 om säkerhet, som medvetet lämnats öppet för
prototypen och dokumenterats som en åtgärd inför nästa fas.

## 5. Prioriterad backlog

Prioriterad enligt MoSCoW. Ordningen följer principen att det som avgör om
systemet är användbart kommer före det som gör det trevligt.

### Must have

| Id | Uppgift | Status |
|---|---|---|
| B1 | Byggmiljö med separation mellan logik och hårdvara | Klar |
| B2 | Mätvärdesmodell med kvalitet och orsakskod | Klar |
| B3 | Drivrutin SHT4x med CRC | Klar |
| B4 | Drivrutin DS18B20 utan blockering | Klar |
| B5 | Validering i fem lager | Klar |
| B6 | Kooperativ schemaläggare | Klar |
| B7 | Telemetriformat med schemaversion och löpnummer | Klar |
| B8 | MQTT-transport med LWT | Klar |
| B9 | Gateway som tar emot och lagrar | Klar |
| B10 | Simuleringsmodell och simulerade givare | Klar |
| B11 | Enhetstester av all logik | Klar, 82 tester |
| B12 | Teknisk dokumentation och beslutsunderlag | Klar |

### Should have

| Id | Uppgift | Status |
|---|---|---|
| B13 | Korskontroller mellan kanaler | Klar |
| B14 | Store-and-forward vid nätavbrott | Klar |
| B15 | Trend och förvarning | Klar, tre omtag, se ADR-006 |
| B16 | Daggpunkt och VPD ombord | Klar |
| B17 | Återställning av hängd I²C-buss | Klar |
| B18 | Seriell transport som reservväg | Klar |

### Could have

| Id | Uppgift | Status |
|---|---|---|
| B19 | Instrumentpanel | Klar, medvetet minimal |
| B20 | Stöd för flera enheter i gatewayen | Klar |
| B21 | Watchdog | Klar |
| B22 | Aggregering min/max/medel per minut | **Ej gjord**, flyttad till nästa fas |

### Won't have i denna fas

Molnplattform, mobilapp, maskininlärning, reglering, produktdesign, TLS och
certifikathantering.

### Ändringar i backloggen under projektet

Tre poster tillkom som inte fanns från början, alla efter att systemet kördes:

| Ny post | Varför den tillkom |
|---|---|
| Höj buffertbudgeten för telemetriramar | Budgettestet visade att ramen inte fick plats. Enheten hade varit helt tyst i fält. |
| Byt linjär extrapolering mot modellidentifiering | 41 falsklarm på sex timmar vid normal lampstart. |
| Lägg brustillägg till derivatakontrollen | 3,6 % av vattenmätningarna underkändes i felfri drift. |

Att de tre inte fanns i den ursprungliga planen är inte ett planeringsmisstag.
Det är vad som händer när man kör systemet istället för att bara läsa koden, och
det är skälet till att planen hade utrymme för det.

## 6. Tidsplan

| Vecka | Fokus | Resultat |
|---|---|---|
| 1 | Kravanalys, givarresearch, arkitektur | Kravlista, ADR-001 och ADR-002, byggmiljö på plats, mätvärdesmodell |
| 2 | Kärnlogik och simulering | Validering, schemaläggare, telemetriformat, termisk modell, första enhetstester |
| 3 | Kommunikation, integration, rättningar | Gateway, hela kedjan igång, tre fel hittade och åtgärdade, ADR-004 och ADR-006 |
| 4 | Färdigställande | Hårdvarudrivrutiner, full testkörning, dokumentation, demo, reflektion |

Veckorna 1 och 2 följde planen. Vecka 3 tog längre tid än planerat, eftersom de
tre felen inte var kända när veckan planerades. Det togs igen genom att
aggregering (B22) flyttades till nästa fas, vilket var rätt prioritering:
aggregering är en bekvämlighet, medan falsklarmen hade gjort hela
förvarningsfunktionen oanvändbar.

## 7. Arbetsfördelning

Projektet är genomfört **individuellt**. Rekommendationen i
projektbeskrivningen är tre studerande, och avvikelsen har stämts av.

Skälet är praktiskt: jag arbetar heltid parallellt med utbildningen, och mina
arbetspass ligger inte förutsägbart nog för att fungera i en grupp där andra
behöver kunna räkna med mig. Jag har genomfört tidigare kurser på samma sätt.

### Hur arbetet strukturerades utan grupp

Ett soloprojekt förlorar två saker som en grupp får gratis: fördelningen av
arbete, och att någon annan ifrågasätter ens beslut. Det första går att hantera
med planering. Det andra behövde ersättas med något.

**Rollväxling istället för rollfördelning.** Arbetet delades upp i faser där jag
medvetet växlade perspektiv istället för att göra allt samtidigt. Kravanalys och
sensorresearch först, utan att skriva kod. Sedan implementation. Sedan test, där
uppgiften uttryckligen var att försöka få systemet att gå sönder snarare än att
bekräfta att det fungerade. Det gav en skarpare uppdelning än om allt skett
parallellt.

**Beslutsunderlag som ersättning för diskussion.** I en grupp motiverar man sina
val muntligt för någon som invänder. Ensam blir det lätt att ett val bara blir
till. De sex ADR-dokumenten skrevs därför löpande under arbetets gång och inte
i efterhand, med alternativ och konsekvenser nedskrivna. Att formulera varför ett
alternativ valdes bort är den närmaste ersättningen för att någon frågar.

**Simuleringen som andra par ögon.** Det här visade sig vara det som faktiskt
fyllde luckan. Alla tre felen som hittades under projektet hittades av att
systemet kördes och mätte på sig självt, inte av att jag läste min egen kod. En
testrigg som kan provocera fram fel på begäran gör invändningar som jag själv
inte hade kommit på.

### Ungefärlig tidsfördelning

| Område | Andel | Kommentar |
|---|---|---|
| Kravanalys och research | 15 % | tyngst i vecka 1, gav ADR-001 och ADR-002 |
| Implementation firmware | 30 % | |
| Simulering och testning | 25 % | mer än planerat, och det var rätt |
| Gateway | 10 % | |
| Dokumentation | 20 % | löpande, inte på slutet |

## 8. Projektrisker

Riskerna identifierades i vecka 1 och följdes upp löpande. Utfall i sista
kolumnen.

| Id | Risk | S | K | Åtgärd | Utfall |
|---|---|---|---|---|---|
| R1 | Givare finns inte att få tag på i tid | Hög | Hög | Bygg mot ett gränssnitt så att komponentval kan ändras sent. Bygg en simuleringsmodell som gör att logiken kan utvecklas parallellt. | **Inträffade.** Hårdvaran blev aldrig tillgänglig. Åtgärden fungerade: hela logiken kunde ändå utvecklas och verifieras, och drivrutinerna är skrivna och kompilerar. Kvarstående konsekvens dokumenterad i tekniska dokumentationen 10.1. |
| R2 | Tid räcker inte, heltidsarbete parallellt | Hög | Medel | Prioritera hårt enligt MoSCoW. Låt Could have vara det som offras. | **Inträffade delvis.** B22 flyttades till nästa fas. Allt under Must och Should blev klart. |
| R3 | Fel som bara syns efter lång drifttid | Medel | Hög | Simulera dygn på sekunder. Testa millis-varvet uttryckligen. | **Åtgärden gav utdelning.** Ett sjudygnstest och ett överflödestest ingår i sviten. |
| R4 | Falsklarm gör systemet oanvändbart | Medel | Hög | Skriv motprov för varje feltest: visa både att fel upptäcks och att friska givare inte larmar. | **Inträffade två gånger.** Båda upptäcktes av just de motproven. Se ADR-006 och ADR-004. |
| R5 | Scope creep, frestelsen att bygga mer | Medel | Medel | Håll fast vid MoSCoW. Kunden har sagt att omfattning inte är det de vill ha. | Hanterad. Panelen hölls minimal, reglering byggdes inte. |
| R6 | Tekniska val kan inte förklaras vid examination | Låg | Hög | Skriv ADR löpande, inte i efterhand. | Hanterad. Sex beslutsunderlag. |
| R7 | Verktygskedjan strular på Mac | Låg | Medel | Kör tester nativt, inte bara på hårdvara. | Inträffade inte. |

### Den risk som blev verklighet

R1 är värd att stanna vid, eftersom åtgärden utformades i vecka 1 och sedan
avgjorde hur projektet kunde gå.

Beslutet att lägga all logik bakom `ISensor`, `IClock` och `ITransport` togs
innan det var känt om hårdvaran skulle bli tillgänglig. Det var en riskåtgärd
och inte en estetisk preferens. När hårdvaran uteblev innebar det att arbetet
kunde fortsätta i full takt istället för att stanna.

Det ska sägas rakt ut att åtgärden inte är gratis och inte löser allt. Den ger
inte svar på om I²C-bussen fungerar med den kabellängd som väljs, eller hur
givarna beter sig vid verklig kondens. Den flyttar de frågorna till nästa fas
istället för att lösa dem. Men den gjorde att allt annat gick att bli klar med,
och att det som återstår är avgränsat och beskrivet.
