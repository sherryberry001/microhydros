# ADR-002: Kommunikationslösning

**Status:** Beslutad
**Datum:** vecka 1, reviderad vecka 3
**Berör:** krav FR-7, FR-8, NFR-4

## Sammanhang

Kunden skriver att de "ännu inte bestämt vilken teknisk lösning som ska
användas" och att "det viktigaste i detta skede är inte vilken specifik
molnplattform som används, utan att prototypen visar att systemet kan
konstrueras för att kommunicera med externa system".

Det är en ovanligt tydlig formulering, och den styr beslutet mer än någon teknisk
detalj gör: **det viktigaste kravet är att inte låsa fast kunden.**

Samtidigt säger kunden att flera odlingsenheter på sikt ska kunna anslutas till
en gemensam tjänst där användare kan följa sina odlingar, analysera historik och
få notifieringar.

## Alternativ

### HTTP/REST mot en egen server

Enkelt att förstå och felsöka med vanliga verktyg. Varje enhet gör POST med sin
mätdata.

Problemet är att enheten måste känna till en URL, ett autentiseringsschema och
ett API-kontrakt. Byter kunden backend byts allt det. Dessutom är HTTP
förhållandevis dyrt per meddelande: TCP-uppkoppling, TLS-handskakning och
HTTP-huvuden för att skicka ett halvt kilobyte mätdata. Och åt andra hållet,
från molnet till enheten, fungerar det inte alls utan att enheten pollar.

### MQTT mot en broker

Enheten känner bara till en broker och ett ämnesnamn. Vem som lyssnar är enhetens
ensak. Det är exakt den frikoppling kunden behöver när de inte har valt
plattform.

MQTT har dessutom två egenskaper som spelar roll i drift:

- **Last Will and Testament.** Brokern får redan vid uppkopplingen ett
  färdigskrivet meddelande som den publicerar om enheten försvinner utan att säga
  hejdå. Utan det ser en enhet som tappat strömmen exakt likadan ut som en enhet
  som bara är tyst just nu, och ingen upptäcker att odlingen står oövervakad.
- **Retained-meddelanden.** Statusramen ligger kvar hos brokern, så en
  instrumentpanel som just startat får senast kända tillstånd direkt istället för
  att visa tomt tills nästa utskick.

Alla större molnplattformar talar MQTT: AWS IoT Core, Azure IoT Hub, HiveMQ,
EMQX. Att byta mottagare blir en adressändring istället för en ombyggnad.

### Bluetooth Low Energy

Fungerar bra för en enhet med en telefon bredvid. Fungerar inte för det kunden
beskriver, nämligen flera enheter som rapporterar dygnet runt till en gemensam
tjänst utan att någon står i rummet. Skulle kräva en gateway ändå.

### Seriell kommunikation till en gateway

Enkel, pålitlig, och uttryckligen tillåten i uppgiften. Nackdelen är att varje
enhet behöver en kabel till en dator, vilket motsäger hela idén om enheter
utplacerade i bostäder och restauranger.

### LoRaWAN

Lång räckvidd och låg effekt, men fel problem. Enheterna står inomhus med ström
och wifi inom räckhåll, och nyttolastbegränsningen på några tiotal byte hade
tvingat fram ett binärt format som gör felsökning svårare.

## Beslut

**MQTT över WiFi som primär väg, med radbaserad seriell utgång som spegel och
reservväg.**

Enheten publicerar på tre ämnen:

```
microhydros/v1/<device_id>/telemetry    mätdata, varje minut, inte retained
microhydros/v1/<device_id>/status       livstecken, var femte minut, retained
```

Nyttolasten är JSON. Versionsnumret ligger både i sökvägen och i nyttolasten.

Parallellt skickas exakt samma ramar på UART i formatet:

```
MH1 <ämne> <json>
```

## Motivering

### Varför versionsnumret ligger i sökvägen och inte bara i nyttolasten

Det ser ut som dubbelarbete men löser ett konkret problem vid uppgradering. När
schemat ändras kan nya enheter publicera på `v2` medan gamla fortsätter på `v1`,
och mottagaren prenumererar på båda under övergången. Utan versionen i sökvägen
måste alla enheter och backend bytas samtidigt, vilket är svårt när enheterna står
hemma hos kunder.

Versionen i nyttolasten behövs ändå, eftersom en mottagare som läser ur en kö
eller en loggfil inte har ämnesnamnet kvar.

### Varför JSON och inte något kompakt binärformat

CBOR eller Protobuf hade gett kanske en fjärdedel så stora ramar. För ett system
som skickar 2 kB i minuten över wifi spelar det ingen roll alls.

Det som spelar roll är att man kan läsa en ram med blotta ögat under felsökning,
att vilken mottagare som helst förstår formatet utan ett schema, och att kunden
kan ta prototypen till vilken konsult som helst utan att först förklara ett eget
binärformat. Det är i linje med kundens huvudkrav att inte låsa fast sig.

Om enheterna någon gång ska gå på batteri eller på mobilnät med datatak blir
avvägningen en annan. Det är en isolerad ändring eftersom `encodeTelemetry` är
den enda funktion som känner till formatet.

### Varför den seriella vägen finns kvar

Tre skäl, och alla tre visade sig användbara under projektet.

**Felsökning.** Man kan koppla in en terminal och se exakt vad enheten skickar,
utan att röra nätverket. När något inte kommer fram säger det direkt om felet
ligger i enheten eller i nätet.

**Uppgiftens krav.** Seriell kommunikation till en gateway är uttryckligen
listad som en godtagbar lösning. Genom att ha båda demonstrerar prototypen att
arkitekturen inte är bunden till ett transportsätt.

**Simuleringen.** Det här blev det viktigaste i praktiken. Simuleringsnoden
skriver samma ramar på stdout, och gatewayens brygga kan inte se skillnad på den
och ett riktigt kort. Det gör att hela kedjan från mätvärde till databas kan
köras och verifieras, och att det som verifieras är exakt samma applikationskod
som ligger på kortet.

### Synkordet MH1

En UART-länk är aldrig ren. En ESP32 skriver ut sin bootloaderlogg i 74880 baud
innan applikationen tar över i 115200, vilket blir obegripliga tecken i
mottagaren. Firmwarens egna loggrader börjar med `[mh]` och ska inte tolkas som
telemetri. Och efter en omstart mitt i en sändning kommer en halv rad.

En mottagare som antar att varje rad är giltig JSON snubblar på allt det här
direkt. Med ett synkord kastar bryggan allt som inte börjar rätt och fortsätter
utan att stanna upp. Bryggan räknar dessutom hur många rader som kastats, så det
syns om länken är dålig istället för att tyst ge färre mätvärden.

### TeeTransport: varför spegeln inte får kvittera

En detalj som är lätt att göra fel. När samma ram går ut på två vägar måste det
vara entydigt vilken väg som avgör om ramen räknas som levererad.

Vi lät MQTT avgöra. Skulle den seriella spegeln kunna kvittera en ram som
levererad så skulle enheten sluta köa ramar när wifi ligger nere, eftersom den
tror att de kom fram. Då hade store-and-forward slutat fungera och mätdata
försvunnit utan att någon märkt det. Det är precis den sortens tysta fel kunden
beskrev från sin förra leverantör.

## Känd begränsning: QoS

PubSubClient, MQTT-biblioteket för Arduino, stödjer bara QoS 0 för publicering.
Det betyder att en ram som försvinner på vägen mellan enhet och broker gör det
utan att någon säger till.

Vi kompenserar på applikationsnivå:

- **Löpnummer** i varje ram, så att mottagaren kan se exakt vilka ramar som
  saknas. Vyn `v_sequence_gaps` i databasen visar dem utan att man behöver skriva
  frågan själv.
- **Store-and-forward** med en kö på 16 ramar, så att ett nätavbrott inte betyder
  förlorad data.
- **Statusram med LWT**, så att en enhet som försvunnit syns som borta.

Det gör förlusten synlig och mestadels reparerad, men inte omöjlig. För skarp
drift är rekommendationen att gå över till ESP-IDF:s egen MQTT-klient som stödjer
QoS 1. Det ligger i [rekommendationerna för nästa fas](../teknisk-dokumentation.md#rekommendationer-inför-nästa-fas).

## Känd begränsning: säkerhet

Prototypen kör mot en öppen broker utan TLS och utan autentisering. Det är
acceptabelt på ett isolerat labbnät och under utveckling, men det är inte
acceptabelt i en skarp installation, och det ska inte glömmas bort bara för att
allt fungerar.

Konkret saknas tre saker:

1. **TLS.** Mätdata går i klartext. Det är inte känslig information i sig, men
   utan TLS kan vem som helst på nätet också skicka påhittad mätdata i enhetens
   namn.
2. **Autentisering per enhet.** Alla enheter borde ha ett eget certifikat eller
   åtminstone ett eget lösenord, så att en komprometterad enhet kan spärras utan
   att alla andra påverkas.
3. **Behörighetsregler på brokern.** En enhet ska bara få publicera på sitt eget
   ämne. Som det är nu kan en enhet publicera i en annans namn.

Konfigurationsfilen för brokern har raderna förberedda och bortkommenterade, så
att det inte ska behöva letas upp från början.

## Konsekvenser

- Enheten är inte bunden till någon molnplattform. Att byta mottagare är en
  ändring i `config.h`.
- Ett nätavbrott upp till sexton minuter kostar ingen data.
- En enhet som försvinner syns som borta inom keepalive-intervallet.
- Prototypen kräver en broker. För demonstration körs Mosquitto i en container,
  vilket är två rader att starta.
- Säkerheten är inte löst och det är dokumenterat som en öppen punkt, inte som
  något vi hoppas att ingen frågar om.
