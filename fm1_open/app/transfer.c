/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The library's application callbacks for MIDI input. They live in a file the build compiles without LTO
 * (sdk/Build.ps1 lists transfer.c): the LTO plugin can let a weak default in the library replace them. */
#include "fm1.h"
#include "fm1b.h"
#include <stddef.h>

void sf2player_play(unsigned note, unsigned velocity);

/* MIDI input from DIN, USB and BLE, any channel. The default would feed the stock instrument. */
void fm1_app_midi_received(unsigned source, const uint8_t *message, size_t bytes)
{
    (void)source;
    if (bytes == 3 && (message[0] & 0xe0U) == 0x80U)
        sf2player_play(message[1], (message[0] & 0xf0U) == 0x90U ? message[2] : 0U);
    else if (bytes == 3 && (message[0] & 0xf0U) == 0xb0U && message[1] == 123U)   /* all notes off */
        fm1b_panic();
}
void fm1_app_sysex(const uint8_t *message, size_t bytes) { (void)message; (void)bytes; }

