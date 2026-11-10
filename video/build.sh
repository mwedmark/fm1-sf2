#!/bin/sh
# Builds fm1_video (see fm1_video.c) from the firmware's own sources with the PC's C compiler.
set -e
cd "$(dirname "$0")"
A=../fm1_open/app
gcc -O2 -w -no-pie -I$A -I$A/synth -I$A/board -I../fm1_open/sdk/include \
  fm1_video.c $A/sf2player.c $A/fm1b.c $A/fm1b_bank.c $A/sf2fx.c $A/sf2out.c $A/song.c $A/transfer.c \
  -Wl,--defsym=text_size=0,--defsym=data_size=0,--defsym=_ram0_data_size=0,--defsym=cache_ram_data_size=0 \
  -o fm1_video
