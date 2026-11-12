# Handover: build and test PR #19 on the Windows machine

Start prompt for a local Claude Code session in `D:\git\fm1-sf2` (JieLi toolchain `D:\JL\pi32`, PowerShell, node, ffmpeg). Written 2026-11-11 by a cloud session that could not build with the JieLi
compiler or run anything on the FM-1. The user writes Swedish; code and documents are English.

Paste everything below the line into the session.

---

Du kör lokalt på Windows-datorn (D:\git\fm1-sf2, JieLi-verktygskedja i D:\JL\pi32, PowerShell, node, ffmpeg). Jag skriver svenska; kod och dokument är på engelska.

BAKGRUND
Draft-PR mwedmark/fm1-sf2#19 (gren claude/nifty-ptolemy-laratq) innehåller två ändringar som en molnsession gjorde utan att kunna bygga med JieLi-kompilatorn eller köra på FM-1:
1. Banken ligger nu i början av användarflashen: FM1B_FLASH_OFFSET 0, FM1B_FLASH_BYTES 536 576 (förut 69 632 och 466 944). Banker som en äldre firmware lagt på 69 632 spelas fortfarande (FM1B_LEGACY_OFFSET, fm1b_device_bind i fm1_open/app/fm1b_bank.c).
2. minimp3 är trimmad till mono MPEG-1 med tre define:ar (MINIMP3_MONO_ONLY, MINIMP3_MPEG1_ONLY, MINIMP3_FRAME_START) som sätts i fm1_open/live_cache/mp3_sound.h. Molnsessionen mätte med Thumb2-proxy: kod 17 170 -> 15 796 B, statisk scratch 16 236 -> 12 972 B, mp3dec_t ca 6 700 -> 5 500 B. Den riktiga siffran för pi32v2 är okänd.
Läs HANDOVER_LIVE_CACHE.md, FM1_TEST.md (avsnitt H och I), FIRMWARE.md ("The bank in the user flash") och fm1_open/third_party/mp3/README.md först.

UPPGIFT, i ordning. Stanna och rapportera vid första fel; rapportera exakt vad du mätte, inget gissat.
1. git fetch origin claude/nifty-ptolemy-laratq och checka ut den. Kontrollera med git log -1 att du står på rätt commit (72cd10e eller senare).
2. Bygg: .\build_fw.ps1 -Name sf2player_live -LiveCache -CacheKB 72 och läs FM1:-raderna. Rapportera bildens storlek, "has N bytes to spare" (före trimningen var det 5 byte; gränsen är 453 921), och heapen (måste vara minst 111 468; med -CacheKB 72 var den 113 388 före trimningen). Bygg också utan -LiveCache och rapportera marginalen. Om länkningen misslyckas: ändra inget i Build.ps1 utan att säga till; kom ihåg att sdk/manifest.json pinnar SHA-256 av Build.ps1.
3. Kör testerna som går lokalt: node test_upload.js, node test_sound_cache.js soundfonts/1mgm.sf2, node test_pack_bank.js soundfonts/1mgm.sf2, node test_packed_engine.js soundfonts/1mgm.sf2. (test_upload.js bygger med wasi-sdk på Windows-sökvägen.)
4. Provstarta i fm1-emu INNAN något flashas (start_emu.ps1 -Tap; kontrollera att "Running sf2player.fwsc?v=" är den nya builden). Kör FM1_TEST.md avsnitt I1-I3 och fyll i resultaten: ny bank i början (make_flash_overlay.js lägger den nu på 0x7C000), gammal bank på 0x8D000 (ska fortfarande spelas), packad bank (gm_32k_packed.fm1b).
5. Först när 2-4 är gröna, och bara om jag säger ja: flasha sf2player_live72.fwsc på FM-1:an. Ha sdk\firmware\FM-1_015.fwsc till hands och kom ihåg säkert läge (håll OCT- och OCT+ vid uppstart). Kör sedan H3-H7 på enheten: uppdatera firmware utan att installera om banken (gamla banken ska spela), installera en bank över 466 944 byte via install.html, upprepad installation ska skippas (samma storlek och CRC), bank över 524 KB ska vägras.
6. Mät på enheten (punkt 3 i HANDOVER_LIVE_CACHE.md): avkodarens hastighet K (miljoner cykler per sekund mono-ljud; designen antar 5-12), kontrolluppgiftens cykeltid, latens för ett ocachat ljud, förlorade toner med 72/96/128 KB cache, CPU-last mot BFP8-bank i flash.

OSÄKERHETER ATT KOLLA
- Okänt om det frusna biblioteket eller uppdateringsladdaren skriver i användarflashens första 68 KiB. Bygget kompilerar ingen av stockens projektkod (project.c m.fl. är bara FM1_HOST_TEST), men det är inte verifierat på enheten. Om något skriver där: stoppa och rapportera.
- Webbplatsens emulatorbanker lägger banken på 0x7C000 (.github/make_site.js); en firmware från före PR:en letar inte där.

REGLER
- Påstå inget bygge- eller testresultat utan faktisk utdata. Om du inte kan köra något, säg det.
- Flasha inget utan mitt uttryckliga ja. Lämna FM-1:an i ett läge där FM-1_015.fwsc kan flashas tillbaka.
- Committa bara dokumentation av mätresultat (RUNNING.md, FM1_TEST.md, HANDOVER_LIVE_CACHE.md) och bara efter att jag sagt ja; pusha till samma gren.
