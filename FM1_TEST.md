# Checklista: SF2-spelaren på en riktig FM-1

Gäller firmwaren på `sf2-layers` från 2026-11-03: 32 röster, spelaren räknar i 32 kHz med omsampling till 44,1 kHz, CPU-spärr,
pan per kanal och SF2-filens egen pan, nivå per zon, hihat-choke, skalstämning, velocitykurva enligt SF2, limiter på utgången,
reverb/chorus per zon, zonernas LFO (vibrato och tremolo), tangentstyrd decay, lågpassfilter med modulationsenvelope och staplade lager (alla zoner på en tangent spelar). **Banken är BFP8**, det enda formatet vi testar framöver (SEA6 spelar fortfarande men testas inte). Bygg om banken
med dagens konverterare, annars saknar den zonnivåer, zonernas reverb/chorus, LFO och filter (en äldre bank spelar fortfarande, men utan dem). Fyll i kolumnen **Resultat** medan du testar. Avbryt vid första steg som inte beter sig som väntat och skriv ned vad som hände.

## Det här behövs

| Vad | Var |
| --- | --- |
| SF2-firmwaren | `dist\sf2player_32v_32k.fwsc` (32 röster) |
| SF2-firmwaren med mute/solo (C8-C13) | `dist\sf2player_32v_mute.fwsc` (2026-11-04: ARP mute, SEQ solo, ALGORITHM kanalvolym, MASTER alltid huvudvolym; samma bank) |
| SF2-firmwaren med infosidan (C15) | `dist\sf2player_32v_info.fwsc` (2026-11-05: REC visar firmware, bank och CRC32), med `dist\gm_32k_bfp8.fm1b` (ny, har namn och datum). Förväntade värden i `dist\sf2player_32v_info.txt` |
| SF2-firmwaren med räknare för tappade toner (C16) | `dist\sf2player_32v_drops.fwsc` (2026-11-05: linjär interpolering igen, raden `DROPPED` på programsidan, SAVE nollställer) |
| SF2-firmwaren med smartare röststöld (C17) | `dist\sf2player_32v_steal.fwsc` (2026-11-05: utklingande toner tas före hållna) |
| Reserv om det knastrar | `dist\sf2player_24v_32k.fwsc` (24 röster, samma bank) |
| Originalfirmwaren (V15) | `fm1_open\sdk\firmware\FM-1_015.fwsc` |
| Workbench för att flasha | <https://open.m-vave.com/fm1-workbench> (Chrome eller Edge, USB-kabel) |
| Konverteraren som skickar banken | `index.html` via `python -m http.server 8765`, sedan <http://localhost:8765/index.html> (RUNNING.md avsnitt 3) |
| Installationssidan (firmware och bank i ett steg) | <http://localhost:8765/install.html>, efter `node make_install.js` (RUNNING.md avsnitt 3b). Fungerar när FM-1 redan kör SF2-firmwaren. |
| SoundFont | `1mgm.sf2` |
| Test-MIDI-filer | `test_emu\midi\*.mid` (byggs med `node make_midi.js`), spelas från Windows (RUNNING.md avsnitt 6) |

Använd en USB-kabel som du vet klarar data, och låt FM-1 vara strömförsörjd under hela flashningen. Stäng andra program som har
FM-1:s MIDI-port öppen (DAW, MIDIberry, loopMIDI-routing) innan du flashar eller skickar en bank.

## A. Säkerhetskopia och vägen tillbaka (först av allt)

Syftet är att bevisa att vi kan komma tillbaka till originalet innan något annat testas.

| # | Steg | Väntat | Resultat |
| --- | --- | --- | --- |
| A1 | Gör en säkerhetskopia av projekt och samplingar med workbenchen. | Filerna sparade på datorn. | |
| A2 | Flasha `dist\sf2player_32v_32k.fwsc` med workbenchen. | Workbenchen godtar paketet och skriver klart. FM-1 startar om. | |
| A3 | Titta på skärmen efter omstart. | `NO BANK` (om user flash saknar bank) eller ett programnamn. Ingen frysning. | |
| A4 | Anslut workbenchen igen medan SF2-firmwaren kör. | Workbenchen hittar FM-1 över USB-MIDI. | |
| A5 | Flasha tillbaka `FM-1_015.fwsc`. | FM-1 startar med originalfirmwaren och fungerar som vanligt. | |
| A6 | Flasha `dist\sf2player_32v_32k.fwsc` igen. | Som A2-A3. | |

Om A4 eller A5 misslyckas: sluta testa och använd M-VAVE:s återställning (workbenchen kan återgå till V15; SDK:n har ett eget
USB-uppdateringsläge). Notera exakt vad som visades.

## B. Ladda in en bank (BFP8)

Kör FM-1 redan SF2-firmwaren kan B1-B3 ersättas av installationssidan: **Installera** skriver firmwaren (om den är en annan version) och
banken i ett svep. Notera i så fall tid och vad sidan visade för varje steg.

| # | Steg | Väntat | Resultat |
| --- | --- | --- | --- |
| B1 | Öppna konverteraren, släpp `1mgm.sf2`, välj **Kodning: BFP8** (förvald), **32 000 Hz**, alla presets, **Konvertera**. | Cirka 465 KB (99,5 %), "ryms i enhetens samplebank". | |
| B2 | Klicka **Skicka till FM-1** (godkänn MIDI/SysEx i webbläsaren). | Sidan visar procent; FM-1 visar `UPLOAD n Rx`. Hela banken tar en stund. | Tid: |
| B3 | När den är klar. | Sidan säger att banken är kontrollerad; FM-1 visar ett programnamn. | |
| B4 | Stäng av och slå på FM-1. | Banken finns kvar (programnamnet visas igen). | |

## C. Panelen

| # | Steg | Väntat | Resultat |
| --- | --- | --- | --- |
| C1 | Spela på tangenterna. | Ljud, inget knaster. | |
| C2 | SELECT: byt program några steg åt båda hållen. | Programnamnet byts och klangen följer. | |
| C3 | OCT- / OCT+. | Oktav ned och upp. | |
| C4 | SCL. | Tangenterna spelar trumsetet. | |
| C5 | KNOB2 (pan), KNOB3 (master reverb), KNOB4 (master chorus) på huvudsidan. | Värdena ändras på rad 9 och hörs. | |
| C6 | HOME: kanalsidan. KNOB1 flyttar markören, SELECT byter kanalens program, KNOB3/4 kanalens reverb/chorus. | Raden och kolumnerna REV/CHO ändras. Tangenterna spelar markerad kanal. | |
| C7 | Titta på rubriken `CHANNELS n/32` och raden `VOICES n/32` medan du spelar. | Talet efter snedstrecket är 32. | |
| C8 | Kanalsidan: vrid ALGORITHM ned och upp medan du spelar. | Bara markerad kanals LVL ändras (4 steg per klick, 0..127) och hörs. | |
| C9 | Kanalsidan och huvudsidan: vrid MASTER. | Hela utnivån ändras på båda sidorna (MASTER är alltid huvudvolymen); ingen kanals LVL ändras. | |
| C10 | Kanalsidan: KNOB2 åt båda hållen, med hörlurar. | Kolumnen PAN visar `L..`/` C `/`R..`, ljudet flyttar sig. | |
| C11 | Spela `mt.mid` (eller en MIDI-fil med flera kanaler). Kanalsidan: markera kanal 2, tryck ARP. Tryck ARP igen. | Kanal 2 tystnar mjukt, utan klick, medan resten spelar vidare; raden blir mörkgrå med `M` sist och ARP lyser. Igen: nästa ton på kanalen hörs, `M` och lampan släcks. | |
| C12 | Markera en kanal, tryck SEQ. Flytta markören till en annan kanal och tryck SEQ. Tryck SEQ igen. | Bara den kanalen hörs (`S` sist, övriga rader mörkgrå, rubriken säger `SOLO`, SEQ lyser på den raden). Solot flyttar till den nya kanalen. Igen: alla hörs, `SOLO` försvinner. | |
| C13 | Mute på en kanal som spelar många toner, titta på `CHANNELS n/32`. | Röstantalet sjunker när kanalen tystnat (tystade kanaler kostar ingen CPU). | |
| C14 | Spela en tät MIDI-fil, titta på `CPU a% MAX p%` (huvudsidan: raden VOICES; kanalsidan: näst sista raden). | Värdena följer musiken; MAX står kvar ungefär en sekund. Raden blir orange när spärren sänkt röstgränsen. Notera CPU och MAX när gränsen sjunker. | |
| C15 | Tryck REC. Jämför med `dist\sf2player_32v_info.txt`. Tryck REC igen. Prova också HOME från infosidan. | Sidan `INFO`: firmwarens namn `SF2PLAYER_32V_INFO.FWSC`, byggtid och CRC32; bankens namn `GM_32K_BFP8.FM1B`, byggtid och CRC32; `SOUNDSET CHECKSUM:` med stor text. Alla tre CRC32 som i .txt-filen (firmwarens skiljer sig från 7-Zips värde för .fwsc-filen, det är väntat). REC tillbaka till samma sida; HOME byter sida. | |
| C16 | Tryck SAVE, spela `MIDIs\R.ASTLEY.Never gonna give you up K.mid` från Windows, titta på raden `DROPPED HELD h TAIL t CPU c`. | `HELD` räknar toner som klippts medan de lät (de du hör falla bort), `TAIL` utklingningar som kortats, `CPU` vad CPU-spärren tog. Notera siffrorna efter låten. `CPU` över 0 betyder att spärren slog till. Emulatorn gav `HELD 379 TAIL 1021 CPU 0` för de första 80 s. | |
| C17 | Med `sf2player_32v_steal.fwsc`: tryck SAVE, spela samma låt som i C16 och jämför `HELD` med C16. | `HELD` klart lägre än med `_drops` (på PC:n 57 % färre för hela låten), `TAIL` högre. Färre toner hörs falla bort. | |

## D. MIDI in (USB, DIN, BLE)

Spela `test_emu\midi\mt.mid` och `drums.mid` från Windows till FM-1, en gång per anslutning.

| # | Steg | Väntat | USB | DIN | BLE |
| --- | --- | --- | --- | --- | --- |
| D1 | `mt.mid`: program 73 på kanal 2, ton C4 på kanal 1 och 2. | Piano och flöjt samtidigt. | | | |
| D2 | `drums.mid`: kanal 10. | Bastrumma, virvel, hihat, crash. | | | |
| D3 | Pitch bend och modulationshjul från ett keyboard. | Tonhöjden böjs; vibrato. | | | |
| D4 | Sustainpedal. | Toner ligger kvar tills pedalen släpps. | | | |
| D5 | `pan.mid` (hörlurar): stråkar på kanal 1, CC10 sveper vänster, höger, mitten. | Ljudet glider jämnt utan knaster; PAN-kolumnen följer. | | | |
| D6 | `drums.mid` igen, med hörlurar. | Trummorna sprids i stereo som i SoundFonten (t.ex. hihat och tom-trummor åt olika håll). | | | |
| D7 | `hats.mid`: öppen hihat, stängd 0,5 s senare (två gånger), sist en öppen ensam. | Den stängda tystar den öppna direkt; den ensamma klingar ut. | | | |
| D8 | Spela några program du känner (piano, stråkar, trummor). | Instrumenten är jämnare sinsemellan än förut, och totalt ungefär lika starka som före zonnivåerna. Inget knaster. | | | |
| D9 | Ett keyboard med anslagskänslighet: spela svagt och hårt på piano. | Tydligare skillnad än förut: svaga toner är mycket svagare (velocity 64 är 12 dB under fullt anslag). | | | |
| D10 | Ett tätt, hårt ackord med många tangenter (eller `load32_all.mid`). | Högt men inte sprucket; nivån andas lite efter ackordet (limitern), inget hårt klipp. | | | |
| D11 | Panelen: program 5 (E Piano 1) och 90 (Warm Pad), jämför med 1 (piano). | E-pianot och padden har tydligt mer chorus (svävning, bredd) än pianot; allt har lite mer reverb än förut. Kolumnen CHO på kanalsidan visar bara kanalens värde, inte zonens tillägg. | | | |
| D12 | Panelen: program 41 (Violin), håll en ton i ett par sekunder utan modulationshjulet. Sedan program 22 (Accordion), en ton under C4. | Violinen börjar rakt och får ett lätt vibrato efter ungefär en kvarts sekund. Dragspelet pulserar svagt i volym (tremolo). | | | |
| D13 | Program 1 (piano): spela samma anslag längst ned och längst upp. | De höga tonerna klingar ut tydligt fortare än de låga. | | | |
| D14 | Program 39 (Synth Bass 1), 82 (Saw Wave) och 90 (Warm Pad): håll en ton. | Synth Bass 1 och Warm Pad mörkare och rundare än förut. Saw Wave blir efter en halv sekund något nasal (resonans kring 4 kHz). Inget surr, pip eller knaster. | | | |
| D15 | Program 4 (Honky-Tonk), 87 (5th Saw Wave) och 53 (Choir Aahs): håll C4. | Honky-Tonk svävar (två ostämda pianon) och är tydligt starkare än piano 1; 5th Saw Wave spelar grundton och kvint; kören fylligare än förut. Not off tystar allt. | | | |

## E. Last och CPU-spärr

Spela filerna en i taget. Lyssna efter knaster och avbrott, och titta på raden `VOICES n/32 CPU a% MAX p%`: `a` är ljudavbrottets
medellast, `p` det högsta blocket den senaste sekunden. Är talet efter snedstrecket under 32 (och raden orange) har spärren sänkt
gränsen. Kanalsidan visar samma CPU-värden på näst sista raden. Skriv gärna ned CPU och MAX i kolumnen.

| # | Fil | Väntat i emulatorn (240 MHz) | Knaster? | CPU / MAX, gräns under 32? |
| --- | --- | --- | --- | --- |
| E1 | `load24_all.mid` | 38-42 % | | |
| E2 | `load32_plain.mid` | 43-44 % | | |
| E3 | `load32_all.mid` (flöjten har nu två lager: två röster per ton) | 45-51 %, topp 58-72 % | | |
| E4 | `steal40.mid` (40 toner, 32 röster) | de äldsta tonerna stjäls | | |
| E5 | `release.mid` | Allt tystnar efter note off | | |
| E6 | `load24_pan.mid` (alla röster panorerade) | Som E1, högst ett par procent mer | | |
| E7 | `load32_lfo.mid` (32 toner på violin och Organ 3, alla med LFO och filter) | 61-64 %, topp 77 % | | |
| E8 | `load32_filter.mid` (32 toner på Saw Wave, Warm Pad, Organ 2, Synth Bass 1, alla med filter) | 60-70 %, topp 76-92 %: `CPU` kan visas | | |

Täta ackord håller limitern under full skala (topp 0,88 i emulatorn). Hörs ändå distorsion i E3 och E4, notera det: det är nivån och inte CPU:n.
Om det knastrar eller gränsen ofta sjunker under 32: flasha `dist\sf2player_24v_32k.fwsc` och upprepa E1-E5 (banken ligger kvar).

## F. SDRAM-test (infosidan, SAVE två gånger)

**OBS (2026-11-08): troligen finns inget SDRAM att testa på en FM-1.** Offentliga öppningar av FM-1 (aroum, lunar-modulator, modd, cyma-1; foton av minst tre
enheter) läser chippets markering som `...-11B8`, och enligt databladets typkod är det en **AC7911B8**: 1 MB flash i kapseln, inget SDRAM (`A` och `B` är
SDRAM-varianterna). Det stämmer med att FM-1 startar från en flash i kapseln och att tillverkarens konfiguration har SDRAM avstängt. En källa säger BA, men visar
själv markeringen `11B8`. Din egen enhets markering har ingen läst. Därför: **kör inte SDRAM-testet (F1–F7) på din enda enhet utan skäl**. Det kan starta om FM-1 och kan
inte ge något annat än "inget svar" om minnet inte finns. ID-raden (F0) är ofarlig (stock-firmwaren läser samma register) och behöver ingen skruvmejsel: den kan jämföras
senare mot en enhet som har SDRAM. Se FORMAT.md, "What is known about the SDRAM so far".

Bakgrund: AC7911BA/BB har 2 respektive 8 MB SDRAM i kapseln enligt databladet, men M-VAVE:s firmware (och bootkonfigurationen i det här
repot, `fm1_open/sdk/tools/pack/boot-config.bin`) har `ENABLE_SDRAM` och `SDRAM_SIZE` på 0. **Med den konfigurationen förväntas testet
misslyckas, och det visar inte att kortet saknar SDRAM**, bara att minnet inte är påslaget. Testet säger bara något om minnet slagits på i
bootkonfigurationen, och det har inte gjorts. Värdena finns nu från JieLis egen SDK (se FORMAT.md, "What is known about the SDRAM so far"):
`ENABLE_SDRAM=1` och `SDRAM_SIZE=2M` (BA) eller `8M` (BB), allt annat som i dag; men hur `2M` lagras vet vi inte säkert, och vad
bootladdaren gör om dess SDRAM-test misslyckas vet vi inte. En felaktig bootkonfiguration kan hindra FM-1 från att starta, och reverse engineering-arbetet
(AL-255/FM-1-RE) hittar ingen bevisad återställning från ROM: ändra den **inte** på enda enheten. Testet skriver till minnet i ett fönster på upp till 8 MB från
`0x04000000` (där JieLis SDK lägger SDRAM; adressstegen skriver också till adresser utspridda över hela fönstret). Om adressen inte är mappad kan FM-1 starta om; ha därför A (originalfirmware och återställning) till hands först.

| # | Steg | Resultat |
| --- | --- | --- |
| F0 | Tryck REC (infosidan) och skriv ned raden `ID ........ SDR ........ PSR ........` under SDRAM-raden, **innan** något test. Det är chippets eget ID-register (`0x10200`) och första registret i SDRAM- och PSRAM-kontrollern (`0x40400`, `0x40500`); stock-firmwaren läser dem också vid varje start. Vad bitarna betyder för SDRAM är inte dokumenterat: värdena kan bara jämföras mellan enheter (BA, BB, chip utan SDRAM). | |
| F1 | Längst ned står `SDRAM  NOT TESTED  (SAVE TWICE)`. | |
| F2 | Tryck SAVE **en** gång: `SDRAM  PRESS SAVE AGAIN TO START` och `NO SDRAM CAN RESTART THE FM-1`. Vänta 5 s: det går tillbaka till det som stod. | |
| F3 | Tryck SAVE **två** gånger inom 5 s. Raden visar fasen och procent: `DATA BUS`, `ADDRESS BUS`, `FILL`, `CHECK`, `CHECK INVERSE`, `WAIT` (3 s), `CHECK LATE`. Noteras: hur lång tid hela testet tar. | |
| F4 | Under testet: håll en tangent. Ljudet ska gå på utan knaster, skärmen ska uppdateras, och CPU-raden på programsidan (REC tillbaka) ska inte gå upp märkbart. Noteras: ljud och CPU. | |
| F5 | Utfallet: `PASS` och storlek (2 eller 8 MB), eller `FAIL DATA BUS`, `FAIL ADDRESS BUS`, `FAIL CELLS`, `FAIL RETENTION`, `TOO SMALL`. Andra raden har detaljerna (felaktiga databitar eller adresslinjer som hex, antal fel, första ordet). Skriv ned allt. | |
| F6 | Startade FM-1 om eller hängde sig? Om ja: under vilken fas (om raden hann visas) och efter hur lång tid? | |
| F7 | Kör om efter en omstart. Samma resultat varje gång? | |

För varje enhet (i RUNNING.md avsnitt 5): chippets markering (`AC7911BA` eller `BB`, en bild), firmware, utfall i F5, om F6 hände.

### Jämförelse mellan enheter (ID-raden i F0)

Syftet är att se **vilka bitar** i `ID`, `SDR` och `PSR` som skiljer sig mellan en enhet med SDRAM (BA eller BB) och en utan. Ett enskilt värde säger ingenting
om SDRAM: ingen källa säger vilken bit som betyder "har SDRAM". Bara skillnader mellan enheter kan peka på en kandidat, och bara om chippmarkeringen på varje enhet
är känd. Skriv hexvärdena exakt som de står (8 siffror) och läs raden **två gånger**, med en omstart emellan: ändras ett värde på samma enhet mellan starterna är det
tillstånd och inte en egenskap, och de bitarna ska inte användas.

| Enhet | Chippmarkering (bild) | Firmware | `ID` | `SDR` | `PSR` | Samma efter omstart? | F5: utfall och storlek | F6: omstart eller hängning | Datum |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| *Förväntat enligt stock-koden* | | | `0x6F00` (kisel-kontroll i `audio_dac_open`, enligt AL-255/FM-1-RE; **inte verifierat** på din enhet) | | | | | | |
| 1 (din) | | | | | | | | | |
| 2 | | | | | | | | | |
| 3 | | | | | | | | | |
| 4 | | | | | | | | | |

Räkna ut vilka bitar som skiljer mellan två rader genom att XOR:a värdena, ett register i taget (bit 0 är den lägsta siffran). Exemplet använder påhittade
värden; byt ut dem mot dina:

```bash
python3 -c "a,b=0x00006F00,0x00006F08; print(f'{a^b:08X}', bin(a^b))"
```

| Jämförelse (rad A mot rad B) | Bitar som skiljer i `ID` | i `SDR` | i `PSR` | Samma på två enheter av samma sort? | Slutsats |
| --- | --- | --- | --- | --- | --- |
| BA mot BB | | | | | |
| BA eller BB mot ett chip utan SDRAM | | | | | |
| Två enheter med samma markering | | | | (ska vara tomt) | |

Hur du läser den:

- Börja med den sista raden: två enheter med **samma** markering ska ge noll skillnad. Om de ger skillnad är registret inte en egenskap hos chippet och kan inte användas.
- En bit är en kandidat för "har SDRAM" bara om den skiljer mellan en enhet med SDRAM och en utan, **och** är lika på alla enheter av samma sort. Stock-koden läser bit 11 i `SDR` och bit 0 i `PSR`
  som "chip straps": börja där, men det är en gissning att de har med SDRAM att göra.
- Med bara din enhet går det inte att dra någon slutsats: skriv ned värdena och spara dem tills du har en andra enhet att jämföra med.
- Ingen kandidat ersätter chippmarkeringen eller SDRAM-testet i F1–F7. Den är en genväg att kolla före ett test, inte ett bevis.

## G. Avsluta

| # | Steg | Resultat |
| --- | --- | --- |
| G1 | Behåll SF2-firmwaren eller flasha tillbaka `FM-1_015.fwsc`. | |
| G2 | Skriv ned resultat och avvikelser i RUNNING.md avsnitt 5. | |

## Det emulatorn inte kunde visa

- Att workbenchen godtar paketet och att vi kan flasha tillbaka (A).
- Bankuppladdningen hela vägen (B): emulatorn tar bara MIDI via DIN och svaren försvinner.
- Skärmen (C, E): emulatorn ritar inte LCD:n.
- SDRAM (F): om kortet har något och vad testet säger om det. Själva testet är körd mot en modell med fel (`node test_sdram.js`), inte mot minne.
- Hur mycket flash-läsningar kostar på riktig hårdvara; emulatorn räknar troligen inte med väntetid.
