# Banks the Pages site hosts

Every `*.fm1b` file in this folder is built into the emulator site (`.github/make_site.js`): it is laid in the emulator's flash as `<name>.flash`
and listed on the MIDI player page. **`default.fm1b` is selected when the site starts**; the sine test bank is always last in the list.

- Plain FM1B banks only (version 1; the site runs the plain firmware, so MP3-packed banks from `pack_bank.js` are refused and the build stops).
- At most 1 MB (the emulator takes 256 flash sectors).
- Everything in this folder is published on the public site. Add only a bank you may redistribute.
