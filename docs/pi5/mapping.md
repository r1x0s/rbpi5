# Controller maps

**English** · [Русский](mapping.ru.md)

## How it works

A map is **C code**, not a file placed next to the player. It is built into the
`knobshim.so` shim, which sits inside `rbp` through LD_PRELOAD, reads the
controller's MIDI from the ALSA sequencer and turns it into the key presses and
values `rbp` understands; LEDs go back the other way.

```
DDJ-FLX4 / DDJ-GRV6 ──MIDI──► knobshim.so (map_flx4.c | map_grv6.c) ──► rbp
                    ◄──LEDs──  rbp_led.c, led_table.c               ◄──
```

| File (`scripts/shims/`) | What is in it |
|---|---|
| `controllers.c` | one row per controller: map name, MIDI port substring, ALSA card id, USB id, keepalive. `RB_MIDI_MAP=auto` uses it to recognise the controller plugged in |
| `map_flx4.c`, `map_grv6.c` | the controller's notes/CCs → `rbp` actions (what each control does) |
| `map_ux.c` | behaviour both maps share: open/leave the browser, the CFX button, SHIFT + platter = search, the held "back to the boot menu" combo |
| `rbp_led.c`, `led_table.c`, `ctrl_map.h` (`struct led_notes`) | which LED is on which note; `-1`/`0` = no such LED |
| `rbp_abi.h` | addresses and keycodes inside `rbp` v1.20 (leave alone unless needed) |
| `test_flx4.c`, `test_grv6.c` + `tests/midi_*.dump` | tests of the maps on MIDI recorded from the real controllers |

Values worth tuning without a rebuild are in `/opt/rblive4/rb.local.conf` on
the Pi (all described in [scripts/device/rb.conf](../../scripts/device/rb.conf)):

| Key | |
|---|---|
| `RB_MIDI_MAP` | `auto` / `flx4` / `grv6` |
| `RB_JOG_PPR` | jog pulses per turn (FLX4 720, GRV6 6449, measured) |
| `RB_JOG_SPEED_PCT` | GRV6: platter speed, 180 = a record at 33⅓ |
| `RB_JOG_LED_STEPS` | GRV6: steps of the jog-centre indicator per turn (72) |
| `RB_JOG_WINDOW_MS` | GRV6: jog speed averaging window (8 ms) |
| `RB_MASTER_CUE_HW` | GRV6 mixes the headphones itself (1) |

After editing `rb.local.conf`, restart the player: `sudo systemctl restart rblive4`.

The "back to the boot menu" combo (FLX4: SHIFT + SMART FADER, GRV6: SHIFT +
BACK, held 2 s) writes `/tmp/rblaunch.request` for a boot menu that watches it.
No menu is part of this repository, so without one it does nothing.

## Changing a map

1. Edit `scripts/shims/map_flx4.c` or `map_grv6.c` (shared behaviour: `map_ux.c`).
2. When behaviour changes, add a check to `test_flx4.c` / `test_grv6.c`.
3. `make shims`: build in Docker → `make check test` → copy the shims to the Pi →
   restart `rbp`. No new tarball, no reboot.
4. Try it on the controller. The previous shims stay on the Pi as
   `/opt/rblive4/*.so.prev`.
5. Commit.

## Capturing a control's MIDI

Numbers for a map are **measured, not guessed**:

```sh
# on the Pi, player stopped (sudo systemctl stop rblive4)
aseqdump -l                       # find the controller's client
aseqdump -p <client>:0            # press/turn: notes, CCs, channels
```

For the GRV6 there are ready scripts: `tools/grv6-capture.sh` (controls) and
`tools/grv6-ledprobe.sh` (walks the LEDs). `tools/aseqdump2dump.py` turns a
capture into a test fixture. MIDI references: [docs/15-flx4-midi.md](../15-flx4-midi.md),
[docs/port/grv6-midi.md](../port/grv6-midi.md).

## Where the maps are

- **FLX4:** upstream's map, plus (branch `pi5-flx4`) CUE/LOOP CALL, the browser
  from the knob, SMART CFX, the Beat FX LED, SHIFT + platter search, the return
  combo, and asking the panel for its control positions at start.
- **GRV6:** decks 1/2, mixer, Beat FX, pads, LEDs (PLAY, CUE, SYNC, loop, pads,
  CH CUE, FX ON, meters, QUANTIZE), the jog-centre indicator, CFX on both
  channels, the return combo. Decks 3/4 do nothing: `rbp` has two players.
- **Not confirmed:** the GRV6 jog-centre indicator's smoothness at 100 Hz, and
  ≥ 1 h sets on each controller. [docs/port/grv6-midi.md](../port/grv6-midi.md)
  predates the 2026-10-22/23 work; the current behaviour is in the code and in
  the `git log` of `pi5-flx4`.
