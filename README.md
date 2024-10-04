# LoopyMSE
A Casio Loopy emulator. WIP, but able to run some games.
Includes functional but slightly buggy/incomplete sound emulation.

## How to use
LoopyMSE must be launched from the command line with these arguments: `<game ROM> <BIOS> [sound BIOS]`

The emulator will automatically load .sav files with the same name as the game ROM in the directory of the executable. If no .sav file exists, the emulator will create one. Specifying the save file to use in the command line may be added at a future date.

The sound BIOS file is optional, and the emulator will run silently if it is not provided. The file may be incorrectly labelled as "Printer BIOS" in older ROM sets.

## Controls
Default keyboard keys:

| Loopy | Keyboard |
| ----- | -------- |
| A | Space |
| B | C |
| C | F |
| D | V |
| L1 | Q |
| R1 | E |
| Up | W |
| Down | S |
| Left | A |
| Right | D |
| Start | Tab |
| Fullscreen | F11 |
| Exit Emulator | ESC (hardcoded) |

## Special Thanks
kasami - sound implementation, dumping the BIOS, HW testing, and many other valuable non-code contributions  
UBCH server - translations and moral support
