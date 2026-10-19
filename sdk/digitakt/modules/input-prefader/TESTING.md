# Input Pre-Fader Sends testing

Module version 0.1.0-experimental, Digitakt I OS 1.54 (stock file SHA-256 `f78ba80f…3cf6`), Core 2.1. Firmware, linked images and emulator state stay on the developer's computer; none of it is committed.

## Evidence tier

Emulator and author hardware. The author (draadpiraat) flashed both exact builds of section 2 on their own Digitakt I, OS 1.54, on 2026-10-19, about 5 minutes each, and reported the results in section 4. Project save/load, pattern changes and sampling from the inputs are not tested.

## How the sends work (OS 1.54)

The render's mixer (0x40071c20) loops over ten channels: tracks 1-8, then IN L and IN R. For each one it builds two pan gains, multiplies them by the LEV gain (0x40071e36) and derives the delay and reverb sends from those post-LEV gains (0x40071eb4). The module saves the pan gains at the first site and, for channels 8 and 9 with the row ticked, uses them at the second. Both handlers replay the instructions they replace; their assembled encodings are identical to the stock ones.

## 1. Site equivalence (patched Unicorn from pinned digiemu)

Each site run in the stock image and in the linked image (Core 2.1 + this module) from identical random register states, MACSR 0xA0 as the audio engine uses, 20,000 cases, comparing all 16 registers, both accumulators and the stack:

| Check | Result |
| --- | --- |
| 0x40071e36, row unticked and ticked: identical to stock, pan gains kept | 0 differences |
| 0x40071eb4, row unticked: identical to stock | 0 differences |
| 0x40071eb4, row ticked, tracks 1-8: identical to stock | 0 differences |
| 0x40071eb4, row ticked, IN L/IN R: equal to stock's send arithmetic on the pre-LEV pan gains | 0 differences (d0 is excluded: stock overwrites it in the next instruction) |

## 2. Running firmware (pinned digiemu c1b5735, firmware-native drawing)

Build: Core 2.1 + this module, `.syx` SHA-256 `e0f55015…4b89`. After boot, the External Mixer block at 0x800018a8 was set by memory write (IN L/R LEV 0, DEL and REV 127; PAN centre, DUAL on as booted). The row was then ticked and unticked with real panel keys (SETTINGS, 9 × DOWN, RIGHT; later LEFT). A code hook read the dry gains and the four sends where the mixer computes them, 600 calls per channel per phase:

| Phase | IN L/IN R dry gain | DEL L/R, REV L/R sends |
| --- | --- | --- |
| Unticked, LEV 0 | 0 | 0 (stock post-fader) |
| Ticked (RIGHT), LEV 0 | 0 | nonzero, -1585545216 each |
| Ticked, LEV 127 | -1610612736 | -1585545216 each |
| Unticked (LEFT), LEV 127 | -1610612736 | -1585545216 each |

The SETTINGS row drew unticked and ticked in the stock checkbox style. No halt.

**With Digi Health and Digi EQ:** build Core 2.1 + Digi Health 1.0 + Digi EQ 1.0.2-experimental + this module, `.syx` SHA-256 `3e8e6422…07d3`, same steps with 11 × DOWN (Digi Health's two rows come first). FAST AUDIO was running: every mixer call came from its SRAM copy, none from the image. The four phases gave the same values as the table above. No halt.

## 3. Build and rules

- `npm run modules:check`: passes (no overlap with any Modwerk Digitakt module's bytes).
- The vendored elekloader kit's `link()` and Modwerk's `linkMods` produce the identical MAIN image, alone and with Digi Health + Digi EQ.
- Memory: 300 bytes of code, 12 bytes of BSS in DDR; no fast SRAM.
- Load (emulator, [cpu-load.json](evidence/cpu-load.json)): CycleClock at 250 MHz, zero-wait memory, the audio interrupt from entry to `rte` over 3,000 blocks of 166,666 cycles. Stock 100,448 mean / 101,241 worst; ticked 100,840 / 101,710 (+392 / +469); unticked 100,771 / 101,564 (+323 / +323). About 0.2% average, 0.3% worst case of a block. Not a hardware measurement.

## 4. Hardware (author's Digitakt I, OS 1.54, 2026-10-19)

Build: Core 2.1 + this module, `.syx` SHA-256 `e0f55015…4b89` (the build of section 2), sent with Elektron Transfer. Source into IN L and IN R. Results as reported by the author:

| Step | Check | Result |
| --- | --- | --- |
| 1 | INPUT SENDS PRE appears in SETTINGS, below SYSTEM, unticked | Yes |
| 2 | Unticked, input DEL/REV up, LEV 0: dry and effect both silent (stock) | Yes |
| 3 | Ticked, LEV 0: effect only, no dry signal | Yes |
| 4 | Ticked, raising LEV: dry returns, effect level unchanged | Yes |
| 5 | Unticked again: stock behaviour; no click when switching | Yes, no clicks |
| 6 | Tracks with DEL/REV unchanged, ticked and unticked | Yes |
| 7 | After a power cycle the row is unticked | Yes |

With Digi Health and Digi EQ on hardware: build `.syx` SHA-256 `3e8e6422…07d3` (section 2). Checks: FAST AUDIO ticked and INPUT SENDS PRE the last row; steps 2-6 again; a Master EQ band change audible; optionally steps 2-3 with FAST AUDIO unticked. The author's report: "tested everything, it checks out correctly"; individual step answers were not itemized for this build.

## Stock flows compared

| Flow | Result |
| --- | --- |
| SETTINGS menu: stock rows and order | Unchanged in the emulator; the new row is added at the end |
| Mixer with the row unticked | Bit-identical to stock (section 1) |
| Tracks 1-8 sends and dry gains | Unchanged in both modes (section 1) |
| Power cycle | Row unticked again on hardware (section 4) |
| Project save/load, pattern change, sampling from the inputs | Not tested |

## Still to test

- Project save/load and pattern changes with the row ticked.
- Sampling from the inputs with the row ticked.
