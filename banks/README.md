# Banks the Pages site hosts

Every `*.fm1b` file in this folder, and in `soundfonts/`, is built into the emulator site (`.github/make_site.js`): it is laid in the emulator's flash as `<name>.flash`
and listed on the MIDI player page. **`default.fm1b` is selected when the site starts**; the sine test bank is always last in the list.

- Plain FM1B banks only (version 1; the site runs the plain firmware, so MP3-packed banks from `pack_bank.js` are refused and the build stops).
- At most 1 MB (the emulator takes 256 flash sectors).
- Everything in this folder is published on the public site. Add only a bank you may redistribute.

## Made by the workflow instead of committed

The `bank` job of `.github/workflows/firmware.yml` can make `default.fm1b` itself: it takes the SoundFont `soundfonts/1mgm.sf2` of this repo (or, if that is not there, fetches one from the repository secret `SF2_URL`:
Settings > Secrets and variables > Actions), converts it with `make_bank.js <format> 32000` (format: the repository variable `BANK_FORMAT`, or `bfp8`;
"Run workflow" takes it as an input too), stores it as the artifact `bank`, and the site build puts it in this folder before building. A bank made
that way replaces a committed `default.fm1b`. Without the secret nothing is made.
