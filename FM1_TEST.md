# Checklista: SF2-spelaren på en riktig FM-1

Gäller firmwaren på `sf2-layers` från 2026-11-03: 32 röster, spelaren räknar i 32 kHz med omsampling till 44,1 kHz, CPU-spärr,
pan per kanal och SF2-filens egen pan, nivå per zon, hihat-choke, skalstämning, velocitykurva enligt SF2, limiter på utgången,
reverb/chorus per zon, zonernas LFO (vibrato och tremolo), tangentstyrd decay, lågpassfilter med modulationsenvelope och staplade lager (alla zoner på en tangent spelar). **Banken är BFP8**, det enda formatet vi testar framöver (SEA6 spelar fortfarande men testas inte). Bygg om banken
med dagens konverterare, annars saknar den zonnivåer, zonernas reverb/chorus, LFO och filter (en äldre bank spelar fortfarande, men utan dem). Fyll i kolumnen **Resultat** medan du testar. Avbryt vid första steg som inte beter sig som väntat och skriv ned vad som hände.

## Det här behövs

| Vad | Var |
| --- | --- |
| SF2-firmwaren | `dist\sf2player_32v_32k.fwsc` (32 röster) |
| Reserv om det knastrar | `dist\sf2player_24v_32k.fwsc` (24 röster, samma bank) |
| Originalfirmwaren (V15) | `fm1_open\sdk\firmware\FM-1_015.fwsc` |
| Workbench för att flasha | <https://open.m-vave.com/fm1-workbench> (Chrome eller Edge, USB-kabel) |
| Konverteraren som skickar banken | `index.html` via `python -m http.server 8765`, sedan <http://localhost:8765/index.html> (RUNNING.md avsnitt 3) |
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
| C8 | Kanalsidan: vrid MASTER ned och upp medan du spelar. | Bara markerad kanals LVL ändras och hörs; ingen hoppande volym. Båda ändlägena ger 0 och 127. | |
| C9 | Tillbaka till huvudsidan (HOME) utan att röra MASTER, vrid sedan lite. | Utnivån hoppar inte, den följer knappen därifrån. | |
| C10 | Kanalsidan: KNOB2 åt båda hållen, med hörlurar. | Kolumnen PAN visar `L..`/` C `/`R..`, ljudet flyttar sig. | |

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

Spela filerna en i taget. Lyssna efter knaster och avbrott, och titta på raden `VOICES n/32`: står det `CPU` efter har spärren
sänkt gränsen.

| # | Fil | Väntat i emulatorn (240 MHz) | Knaster? | `CPU` visas? |
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
Om det knastrar eller `CPU` visas ofta: flasha `dist\sf2player_24v_32k.fwsc` och upprepa E1-E5 (banken ligger kvar).

## G. Avsluta

| # | Steg | Resultat |
| --- | --- | --- |
| G1 | Behåll SF2-firmwaren eller flasha tillbaka `FM-1_015.fwsc`. | |
| G2 | Skriv ned resultat och avvikelser i RUNNING.md avsnitt 5. | |

## Det emulatorn inte kunde visa

- Att workbenchen godtar paketet och att vi kan flasha tillbaka (A).
- Bankuppladdningen hela vägen (B): emulatorn tar bara MIDI via DIN och svaren försvinner.
- Skärmen (C, E): emulatorn ritar inte LCD:n.
- Hur mycket flash-läsningar kostar på riktig hårdvara; emulatorn räknar troligen inte med väntetid.
