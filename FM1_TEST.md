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
| SF2-firmwaren med MIDI-filspelaren (C18-C20) | `dist\sf2player_midiplay.fwsc` (grenen `sf2-midiplay`, 2026-11-05: en låt i RAM, PLAY spelar och stoppar). Låtsidan: <http://localhost:8765/song.html> |
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
| A7 | Stäng av. Håll OCT− och OCT+ och slå på, håll tills skärmen visar `SAFE MODE`. Anslut installationssidan. | `SAFE MODE`, inget ljud från tangenterna, sidan hittar FM-1 och kan installera. Slå av och på utan knappar: spelar som vanligt. | |

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
| C18 | Med `sf2player_midiplay.fwsc`: öppna `song.html`, släpp `MIDIs\R.ASTLEY.Never gonna give you up K.mid` på sidan och klicka **Send to FM-1**. Notera tiden sidan visar. | Sidan säger `Done in n s`. FM-1 visar `UPLOAD n R0` och raden `SONG R.ASTLEY.NEVER G 3:38  PLAY STARTS`. | |
| C19 | Tryck PLAY på FM-1. Lyssna en minut och tryck PLAY igen. Tryck PLAY en gång till. | Låten spelar i rätt tempo, raden blir grön med tiden (`PLAY ... 0:12/3:38`) och PLAY lyser. Andra trycket: alla toner tystnar inom en sekund och lampan släcks. Tredje trycket: låten börjar om från början med rätt instrument. | |
| C20 | Tryck REC medan låten spelar och notera `FREE RAM n KB FOR A SONG`. Prova HOME (kanalsidan), ARP och SEQ medan låten spelar. | Infosidan visar låten och hur mycket RAM som är ledigt. Den siffran avgör hur stora låtar som får plats. Kanalsidan visar låtens kanaler, och mute och solo fungerar som för MIDI från datorn. | |

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

## F. SDRAM-test (borttaget 2026-11-08)

**Kört på FM-1 den 2026-11-08** (`sf2player_sdram.fwsc`): SAVE två gånger på infosidan startade om FM-1. Adressen `0x04000000`, där
JieLis SDK lägger SDRAM, svarar inte. Det stämmer med att chippet är en AC7911B8 utan SDRAM (FORMAT.md, "What is known about the SDRAM
so far"). Det skiljer inte helt "inget SDRAM" från "SDRAM avstängt i bootkonfigurationen" (`ENABLE_SDRAM` 0); bara chippets märkning
gör det. Testet, `sdram_test.c` och `test_sdram.js` är borttagna; SAVE nollställer nu bara räknaren för tappade noter. Det som finns är
de cirka 514 KB SRAM, och live-cachen (FORMAT.md) använder dem.

## H. Live-cachen: mätningar (packad bank)

Firmware `dist\sf2player_live84.fwsc` (2026-11-11, `-LiveCache -CacheKB 84 -CacheFrames 2`, minimp3 trimmad för mono 32 kHz, 84 KB cache med samma lediga heap som 72 KB hade förut; firmwarens CRC32 `E3468A2D` sedan den också skickar mätvärdena över MIDI till `stats.html`, RUNNING.md 3d; se `dist\sf2player_live84.txt`)
med banken `dist\gm_32k_packed.fm1b` (241 520 byte). Kontrollerad i fm1-emu (startar, spelar, infosidan räknar); **inte på FM-1 än**.
Startar den inte: säkert läge (håll OCT- och OCT+ vid start, FIRMWARE.md) och flasha tillbaka. Infosidans två nedersta rader räknar från att REC trycktes:

- `CACHE H M E F L P W`: träffar (ljudet fanns i cachen), missar (avkodades), ljud som slängts ur cachen, gånger utan plats,
  toner som tog slut innan ljudet hann bli klart (raden blir orange), lås just nu, ljud som väntar just nu.
- `MP3 n FR u US/FR CYC c MAX m MS`: avkodade MP3-ramar, mikrosekunder per ram (ljudavbrottets andel av tiden ingår), styrtaskens varv i ms, snitt och längsta.
  En ram är 36 ms ljud. **K** (miljoner cykler per sekund ljud) = 240 × `u` / 36 000. Emulatorn (240 MHz): 1 800 US/FR, alltså K ≈ 12, CYC 10, MAX 42.

Emulatorn, 2026-11-11, första 60 s av Never gonna give you up (`node midi_script.js ... --seconds=60`, REC först): `CACHE H1622 M159 E144 F937 L8 P16 W0`,
`MP3 878 FR 2191 US/FR CYC 11 MAX 43 MS` (K ≈ 15; `P16`: skriptet klipper låten mitt i, så 16 toner ligger kvar). Programsidan vid 40 s / 54 s: packad bank
`CPU 43% MAX 49%` / `39% MAX 39%`, `DROPPED HELD 122 TAIL 1320`; BFP8-banken med samma firmware `CPU 45% MAX 60%` / `39% MAX 40%`, `HELD 121 TAIL 1330`.
Cachen kostar alltså ingen CPU i ljudavbrottet, men 72 KB räcker inte helt: 8 toner kom för sent. `-CacheFrames 4` hjälpte inte (`L10`).
Med 84 KB (`sf2player_live84.fwsc`, efter att cachens egna buffertar krympts 10,9 KB): `CACHE H1696 M89 E73 F276 L4 P0 W0`, `MP3 517 FR 2203 US/FR CYC 11 MAX 43 MS`.

**På FM-1, 2026-11-11** (`sf2player_live84.fwsc` utan MIDI-mätvärden, CRC32 `24D0C78C`, installerad med `install.html`): startar och spelar. Infosidan:
`CACHE H1990 M27 E19 F0 L0 P0 W0`, `MP3 144 FR 6493 US/FR CYC 10 MAX 214 MS`. Inga toner för sent. Avkodningen tar 6,5 ms per ram, tre gånger
emulatorns tid (K ≈ 43). `MAX 214 MS` är inte förklarat. Spelat efter REC: Never gonna give you up i 31 s och lite Eine kleine Nachtmusik.

**Hela Never gonna give you up på FM-1, mätvärdena över MIDI** (`E3468A2D`, `stats.html`, 214 s spelat): 6 627 träffar, 426 avkodningar, 407 slängda ljud,
373 gånger utan plats, **25 toner för sent**, alltid i täta partier och samtidigt som "utan plats": cachen är full av ljud som klingande röster håller fast.
Ljudavbrottet tar 63 % av CPU:n (topp 83 %) under låten, och den tiden ingår i 6,8 ms per ram: avkodaren själv tar cirka 2,5 ms (K ≈ 15, som i emulatorn).
Styrcykeln: 10 ms i vila, 33 ms under låten och cirka 90 ms (upp till 128) en gång i sekunden. DROPPED: held 502, tail 5 131, CPU 3.
Med högst två skärmrader per uppdatering (`95FCED19`): längsta styrcykel 55 ms i stället för 90, men 33 toner för sent. Med klippta utklingningar
när cachen saknar plats (`85D99FA6`): **19 toner för sent**, 245 utklingningar klippta.

| # | Steg | Väntat | Resultat |
| --- | --- | --- | --- |
| H1 | Flasha firmwaren och banken. Tryck REC och spela några toner på panelen, olika program. | Infosidan visar `GM_32K_BFP8.FM1B`, 241 520 byte. `M` och `MP3` ökar vid första tonen på ett nytt ljud, `H` när samma ton spelas igen. | |
| H2 | Tryck REC två gånger (räknarna nollställs), spela `MIDIs\R.ASTLEY.Never gonna give you up K.mid` från Windows i en minut. Skriv av båda raderna. | `L` 0 eller nära; notera `US/FR`, `CYC`, `MAX`. | |
| H3 | Samma låt, titta på programsidans `CPU n% MAX m%` och `DROPPED`. Jämför med samma låt på BFP8-banken (C16). | Ungefär samma CPU som BFP8-banken (avkodningen går i styrtasken, inte i ljudavbrottet). | |
| H4 | Lyssna: hörs fördröjning på första tonen av ett nytt ljud? | Högst några ms (en ram avkodas på ungefär 2 ms). | |

## G. Avsluta

| # | Steg | Resultat |
| --- | --- | --- |
| G1 | Behåll SF2-firmwaren eller flasha tillbaka `FM-1_015.fwsc`. | |
| G2 | Skriv ned resultat och avvikelser i RUNNING.md avsnitt 5. | |

## I. Banken i början av användarflashen (2026-11-11)

Banken ligger nu på användarpartitionens början (`FM1B_FLASH_OFFSET` 0, upp till 536 576 byte) i stället för efter M-VAVE:s 68 KiB projektyta (69 632, upp till 466 944).
Prova i fm1-emu (`start_emu.ps1 -Tap`) före FM-1:an, och ha `FM-1_015.fwsc` till hands.

| # | Steg | Resultat |
| --- | --- | --- |
| I1 | Bygg och starta den nya firmwaren i fm1-emu med en bank på ca 450 KB som `make_flash_overlay.js` lagt i början (`&flash=`). Spelas den? | Ja i fm1-emu 2026-11-12 (`main` med PR 19, bild 453 756 byte, CRC32 `D6E63696`; BFP8-bank 464 636 byte, 60 s av `astley60_rec.script`, 240 MHz): last ca 48 % under låten, 23 % i vila, infosidan visar banken med CRC32 `BCC6ED6B`. Utan bank: tyst, last 11 %. Ingen bank över 466 944 byte provad. |
| I2 | Samma firmware, bank lagd på det gamla stället (69 632: kör `make_flash_overlay.js` med startadress `0x8D000`). Spelas den? (Äldre bank ska fortfarande fungera.) | Ja i fm1-emu 2026-11-12 (samma bygge och bank; de första 68 KiB tömda till 0xFF så att bara det gamla stället har en bank): last ca 50 % under låten, samma bank och CRC på infosidan. |
| I3 | På FM-1:an med en bank från förut: flasha den nya firmwaren (utan att installera om banken). Spelas den gamla banken fortfarande? | |
| I4 | install.html: installera en bank på över 466 944 byte (t.ex. en 500 KB BFP8-bank). Skrivs den, och skippas en andra installation (samma storlek och CRC)? | Skrivs: ja på FM-1:an 2026-11-13. Lokal install.html med `sf2player_44k_20261113.fwsc` (44,1 kHz, `build_fw.ps1 -Rate 44100`, PR 30) och den packade 44,1 kHz-banken `gm_44k_fill.fm1b`, 533 268 byte (99,4 % av flashen); banken spelar och låter rätt. En andra installation skippar både firmware och bank (samma dag). |
| I5 | Efter I4: ligger inget kvar av den gamla banken som spelar (stäng av, sätt på)? Spelar den nya banken? | |
| I6 | Försök installera en bank på över 524 KB. Vägrar sidan? | |
| I7 | Äldre firmware (före detta ändring) och en bank på över 456 KB: vägras uppladdningen med ett fel, utan att skriva? | |

## Det emulatorn inte kunde visa

- Att workbenchen godtar paketet och att vi kan flasha tillbaka (A).
- Bankuppladdningen hela vägen (B): emulatorn tar bara MIDI via DIN och svaren försvinner.
- Skärmen (C, E): emulatorn ritar inte LCD:n.
- Hur mycket flash-läsningar kostar på riktig hårdvara; emulatorn räknar troligen inte med väntetid.
