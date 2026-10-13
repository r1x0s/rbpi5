# 19 — DDJ-GRV6 MIDI

The AlphaTheta DDJ-GRV6 as this fork's control surface. Written
like [15 — DDJ-FLX4 MIDI](15-flx4-midi.md): every number below is **measured** on
the unit unless it says otherwise. No published MIDI list was found for the GRV6
(AlphaTheta's support pages carry only the user manual; searched 2026-10-13), so
there is nothing to cross-check against except the FLX4, whose layout it largely
shares.

**Status: mapped, not yet run on the unit.** `scripts/shims/map_grv6.c` is written
from the tables below and passes its fixture test (`test_grv6`); selecting it on the
Pi (`RB_MIDI_MAP=grv6`) and checking each control there is the next step. See
[The map](#the-map-map_grv6c) for what it binds and why.

## Source of the numbers

* **Capture:** 2026-10-13, `tools/grv6-capture.sh` (one prompt per control, raw
  `aseqdump` output per labelled block) on the Pi 5, GRV6 firmware as reported by
  USB `bcdDevice 1.02`. 68 blocks, 30 720 lines. The raw capture is kept locally,
  not in git.
* **Channels are `snd_seq` channels (0-based)**, as `aseqdump` prints them. A
  1-based "MIDI list" channel is this number + 1. docs/15 explains why this
  conversion is the most load-bearing detail of a map.
* Two blocks were captured wrongly by the operator (Beat FX `BEAT < / BEAT >` and
  the FX channel select buttons, first pass) and were re-captured; only the
  re-captures are used below.
* Unless noted, every button sends Note On velocity 127 on press and Note Off on
  release, and every knob/fader sends a **14-bit pair**: MSB on CC *n*, LSB on CC
  *n*+32 (the FLX4's convention).

## The device

| | Measured |
|---|---|
| USB | `2b73:004c`, "AlphaTheta Corporation DDJ-GRV6", `bcdDevice 1.02`, high speed. Interfaces: 5 × audio (`snd-usb-audio`), 1 × HID, 1 × vendor-specific (no driver) |
| ALSA card | id `DDJGRV6` (card number varies per boot; address it by id) |
| Sequencer | one port, `DDJ-GRV6 MIDI 1` |
| Playback | 4 ch, `S24_3LE` (altset 1) or `S16_LE` (altset 2), 44100 / 48000 Hz |
| Capture | 2 ch, same formats and rates |
| Output pairs | **ch 0/1 = MASTER L/R, ch 2/3 = HEADPHONES L/R** (the FLX4's layout, upstream's default `RB_AUDIO_MAP`). Master measured by ear; the headphone L/R order is deduced from the hardware MASTER CUE test (master-left was heard on the ear that channel 2 reached), to be confirmed by ear |
| Hardware MASTER CUE | pressing MASTER CUE mixes master into the headphones **in the controller**, with no host involved |
| Idle traffic | **none**: 0 events in 60 s untouched |

## Deck 1 (ch 0)

| Control | Message | FLX4 |
|---|---|---|
| PLAY/PAUSE | note 11 (0x0B) | same |
| CUE | note 12 (0x0C) | same |
| SHIFT | note 63 (0x3F) | same |
| SHIFT + PLAY | note 71 (0x47) | **differs** (FLX4: 14) |
| SHIFT + CUE | note 72 (0x48) | same |
| BEAT SYNC | note 88 (0x58), short and long press alike | short press same; **no separate long-press note** (FLX4: 92) |
| loop IN | note 16 (0x10) | same |
| loop OUT | note 17 (0x11) | same |
| 4 BEAT / EXIT | note 77 (0x4D) | same |
| CUE/LOOP CALL < | note 81 (0x51) | same |
| CUE/LOOP CALL > | note 83 (0x53) | same |
| KEY SYNC | note 101 (0x65) | not on the FLX4 |
| MASTER TEMPO | note 26 (0x1A) | not on the FLX4 |
| MEMORY | note 61 (0x3D) | not on the FLX4 (61 is the FLX4's SHIFT + CALL >) |
| jog top, touch | note 54 (0x36) | same |
| jog top, turn | CC 34 (0x22), relative, 0x40-centred | same |
| jog side ring, turn | CC 33 (0x21), relative, 0x40-centred | same |
| SHIFT + jog | touch note 103 (0x67), turn CC 41 (0x29) | same |
| TEMPO fader | CC 0 / 32 | same |
| channel CUE | note 84 (0x54) | same |

**Jog resolution.** Ten steady clockwise turns of the top: **64 488 counts,
6 448.8 per revolution** (`aseqdump2dump.py --revs 10`; 0x40-centred confirmed,
largest step 6). One-turn captures: top 6 536, side ring 6 138 (single hand turns,
coarse). The FLX4 measured 720. So the GRV6's jog has roughly **9×** the
FLX4's resolution; a map that reuses the FLX4's jog scaling would turn the
platter about nine times too fast. Exact counts/rev beyond the hand-count error
(about 1 %): TODO.

## Decks 2–4 and the DECK layers

| | Measured |
|---|---|
| Deck 2 | ch 1: PLAY note 11, jog ring CC 33. The rest is **expected** to mirror deck 1 on ch 1 (as on the FLX4) and is checked control by control at bring-up row G5 |
| Deck 3 / 4 | ch 2 / ch 3 (PLAY = note 11 on that channel). CH3 channel CUE = note 84 on ch 2 |
| DECK select | pressing a side's DECK button sends **note 114 (0x72)** on the *target* deck's channel (press/release), then **note 60 (0x3C) On** on the target channel and **Note Off 60 on the previously active** channel: note 60 is a layer-active state |
| On a switch to 3 or 4 | additionally the new layer's TEMPO position (seen once: CC 0 = 64, CC 32 = 0 on ch 2) and **two MIDI System Reset** events (`aseqdump`: `Reset`). Not seen on the switch back to 1 / 2 |

A map for a 2-player rbp has to decide what to do while a 3/4 layer is active. The note-60 state gives it an exact signal to follow.

## Mixer (ch 6, except where a deck channel is named)

| Control | Message | FLX4 |
|---|---|---|
| channel fader (ch1–ch4) | CC 19 / 51 on the deck's channel (0–3) | same scheme |
| TRIM (CH1) | CC 4 / 36 on ch 0 | same |
| EQ HI / MID / LOW (CH1) | CC 7/39, 11/43, 15/47 on ch 0 | same |
| Sound Color FX knob, CH1 | CC 23 / 55 on ch 6 | same (FLX4 CH2 = 24; CH2–4 on the GRV6: TODO) |
| crossfader | CC 31 / 63 | same |
| MASTER LEVEL | CC 8 / 40 | same |
| **BOOTH LEVEL** | **CC 9 / 41** | not on the FLX4 |
| HEADPHONES LEVEL | CC 13 / 45 | same |
| HEADPHONES MIX | CC 12 / 44 | same |
| MASTER CUE | note 99 (0x63) | same |

TRIM, EQ and the Sound Color FX knob were captured on CH1 only; CH2–4 are
**expected** on their own channels (TRIM/EQ) or neighbouring CCs (Sound Color FX)
and are TODO.

## Browse, LOAD and the browse-area buttons (ch 6)

| Control | Message | FLX4 |
|---|---|---|
| browse knob, turn | CC 64 (0x40), relative **0x01 = clockwise / 0x7F = counter-clockwise** | same |
| browse push | note 65 (0x41) | same |
| SHIFT + browse push | note 66 (0x42) | same |
| browse tilt **DOWN / UP / LEFT / RIGHT** | notes **58 / 56 / 60 / 62** (0x3A / 0x38 / 0x3C / 0x3E) | not on the FLX4 |
| LOAD deck 1 / 2 | note 70 / 71 (0x46 / 0x47) | same |
| SHIFT + LOAD deck 1 / 2 | note **88 / 89** (0x58 / 0x59) | **differs** (FLX4: 104 / 122) |
| BACK | note 101 (0x65) | not on the FLX4 |
| VIEW | note 122 (0x7A) | not on the FLX4 (122 is the FLX4's SHIFT + LOAD 2) |
| PREVIEW | note 54 (0x36) | not on the FLX4 |
| DISCOVER | note 53 (0x35) | not on the FLX4 |

The tilt was performed in the order down, up, left, right (operator's note), not
in the order the prompt named; the table uses the performed order.

## Beat FX (ch 4)

| Control | Message | FLX4 |
|---|---|---|
| Beat FX knob (effect select) | **one note per position, 32–45 (0x20–0x2D): 14 positions** as it was turned | **differs** (FLX4: an FX SELECT button stepping one note, 99, + SHIFT 100) |
| BEAT < / BEAT > | note 74 / 75 (0x4A / 0x4B) | same |
| **FX channel select buttons** (six; the FLX4 has a CH SELECT lever) | **one note per button, all on ch 4**: **1 = 16, 2 = 17, 3 = 18, 4 = 19, MST = 20, SP = 22** (0x10–0x13, 0x14, 0x16). A **radio group**: pressing a button sends Note On for it and **Note Off for the previously selected one** | **differs** (FLX4: 16 on ch 4 and 17 on ch 5, two legs) |
| LEVEL/DEPTH | CC 2 / 34 | same |
| FX ON/OFF | note 71 (0x47) | same note (FLX4 sends it on one leg per target channel) |

**FX channel select buttons.** Printed labels **1, 2, 3, 4, SP, MST**, two per row
in three rows. Measured by a second capture pressing them in label order
(2026-10-13): `On 16 / Off 20`, `On 17 / Off 16`, `On 18 / Off 17`, `On 19 / Off 18`,
`On 22 / Off 19`, `On 20 / Off 22`. So the numbering is the FLX4's (16 = CH1,
17 = CH2, 20 = MASTER), and the Note Off is a **deselect** of the previous button,
not a release: a map must act on Note On only.

**Beat FX knob.** One note per click, strictly in order: the sweep went 33 → 45
turning right and 45 → 32 turning back. With the effect names printed left to
right (owner, 2026-10-13):

| Note | GRV6 effect | rbp position (`K_BFXTYPE`, read off rbp's panel, `fx_zone.c`) |
|---|---|---|
| 32 | DELAY | 0 DELAY |
| 33 | ECHO | 1 ECHO |
| 34 | LC ECHO | — (rbp has none) |
| 35 | SPIRAL | 3 SPIRAL |
| 36 | HELIX | 4 HELIX |
| 37 | REVERB | 5 REVERB |
| 38 | FLANGER | 6 FLANGER |
| 39 | PHASER | 7 PHASER |
| 40 | FILTER | 8 FILTER |
| 41 | TRANS | 9 TRANS |
| 42 | PITCH | 12 PITCH |
| 43 | ROLL | 10 ROLL |
| 44 | MOBIUS (saw) | — |
| 45 | MOBIUS (triangle) | — |

Eleven of fourteen match by name. rbp's PING PONG (2), SLIP ROLL (11) and VINYL
BRAKE (13) have no GRV6 position; the GRV6's LC ECHO and both MOBIUS have no rbp
effect. What the three leftovers do is a map decision.

## Pads

| | Measured | FLX4 |
|---|---|---|
| pad mode buttons (deck 1, ch 0) | notes **27, 30, 32, 34** (0x1B, 0x1E, 0x20, 0x22), left to right, top row then bottom | same notes |
| deck 1 pads | ch 7: mode 1 (27) notes 0–7, mode 2 (30) 16–23, mode 3 (32) 32–39, mode 4 (34) 48–55 | same scheme |
| deck 1 pads + SHIFT | ch 8 (seen: HOT CUE pad 1 = note 0) | same |
| deck 2 pads | ch 9 (seen: HOT CUE pad 1 = note 0, mode note 27 on ch 1) | same |

Printed pad modes (owner, 2026-10-13): **HOT CUE (27), STEMS (30), BEAT JUMP (32),
SAMPLER (34)**; with SHIFT: **KEYBOARD, PAD FX, BEAT LOOP, KEY SHIFT** (on the same
four buttons, in that order: **105, 107, 109, 111** on the deck channel, measured
with SHIFT held: the FLX4's SHIFT + mode notes exactly). rbp has four pad modes: HOT CUE,
AUTO BEAT LOOP, SLIP BEAT LOOP, BEAT JUMP (docs/15). By name, HOT CUE, BEAT JUMP and
BEAT LOOP have rbp equivalents; STEMS, SAMPLER, KEYBOARD, PAD FX and KEY SHIFT do
not.

## The map (`map_grv6.c`)

Selected with `RB_MIDI_MAP=grv6`, which also gives the audio shim the `DDJGRV6`
card and the MIDI side the `GRV6` port name (the `grv6` row of
`scripts/shims/controllers.c`). The FLX4 stays the table's default row.

| Control | Goes to | Note |
|---|---|---|
| decks 1/2 (ch 0/1) | rbp players 1/2 | PLAY, CUE, loop IN/OUT, 4 BEAT/EXIT, jog touch, channel strip: as the FLX4 map |
| decks 3/4 (ch 2/3) | nothing, logged | rbp has two players (D6) |
| BEAT SYNC | SYNC, once per press | no long press: the GRV6 has no separate note for it |
| MASTER TEMPO | rbp's key lock (`K_MT`) | name match |
| KEY SYNC, MEMORY, CUE/LOOP CALL < > | nothing, logged | rbp has no KEY SYNC; the others' rbp keycodes are unidentified |
| pad modes | HOT CUE → HOT CUE, BEAT JUMP → BEAT JUMP, SHIFT+BEAT LOOP → AUTO BEAT LOOP, SAMPLER → SLIP BEAT LOOP | by name (D19); STEMS, KEYBOARD, PAD FX, KEY SHIFT reach nothing |
| pads | rbp's pads 1–8 | bases 0, 32, 48 measured; BEAT LOOP's 96 expected (the FLX4's) |
| SHIFT + HOT CUE pad | deletes that hot cue | as the FLX4 map |
| Beat FX knob | rbp's effect, absolute | 11 by name; LC ECHO → PING PONG, MOBIUS saw → SLIP ROLL, MOBIUS tri → VINYL BRAKE (D18) |
| FX channel 1 / 2 / MST | Beat FX target deck 1 / deck 2 / MASTER | Note On only; 3, 4, SP change nothing (D17) |
| browse turn, push, SHIFT+push | list step, select, SOURCE | as the FLX4 map |
| browse tilt UP / DOWN / LEFT / RIGHT | step up / step down / BACK / select | the keyboard map's arrows, BACKSPACE and ENTER |
| BACK | BACK | |
| LOAD, SHIFT+LOAD 1 / 2 | LOAD, TRACK FILTER / SHORTCUT | the FLX4 map's actions on the GRV6's notes |
| VIEW | BROWSE (opens / closes the browser) | D25: BACK never leaves the browser; rbp's BROWSE key does. upstream's JP21 map binds its VIEW the same way |
| DISCOVER | SOURCE (the media list: USB 1, …) | D25: one press; SHIFT + browse push still does the same |
| PREVIEW | nothing, logged | no rbp equivalent |
| MASTER LEVEL | the HDMI mirror's level only | as the FLX4 map |
| BOOTH LEVEL | nothing | the controller's own booth output (D20) |
| MASTER CUE | the controller only | done in hardware (measured), so rbp's own master cue is held off; `RB_MASTER_CUE_HW=0` gives the FLX4 map's behaviour |
| LEDs, meters | PLAY, CUE, BEAT SYNC, LOOP IN/OUT, the pads (all four modes), CH1/CH2 CUE, Beat FX ON/OFF, the channel meters | measured 2026-10-13 with `tools/grv6-ledprobe.sh`: every LED that lit is its control's own input note on its own channel (PLAY 11, CUE 12, SYNC 88, IN 16, OUT 17 on ch 0/1; pads ch 7/9, bases 0 / 96 / 48 / 32 for HOT CUE / BEAT LOOP / SAMPLER / BEAT JUMP; CH CUE 84 on ch 0/1; FX ON/OFF 71 on ch 4; meter CC 2 on ch 0). Pads are blue only, lit at velocity 127 and dark at 1–120. The GRV6 lights MASTER TEMPO, DECK, the pad mode buttons, MASTER CUE, the FX channel buttons and LOAD **by itself**, so the map leaves those alone. No light: 4 BEAT/EXIT, MEMORY, CALL < >, BACK, VIEW, DISCOVER. Both decks' PLAY blink at one cadence (deck 1's), though rbp asks 500 ms / 250 ms |

Two numbers are new to the GRV6. The jog's default `RB_JOG_PPR` is **6449**, and
its speed is reported once per **8 ms window** (`RB_JOG_WINDOW_MS`; 0 = every
message, as on the FLX4) rather than per message, because the GRV6 sends about
1 800 messages per turn and a per-message speed on a millisecond clock jumps by
2× between neighbours. Its position field is sent in the FLX4's units (720 per
turn), the units rbp has been seen to accept.

## Not measured yet

* **LEDs**, and whether the host must send a **keepalive** for the controls or
  the audio to stay alive (the FLX4 needs one every 200 ms; docs/15). The idle capture only says
  the GRV6 sends nothing on its own.
* Deck 2 beyond PLAY and the jog ring; decks 3/4 beyond PLAY; mixer CH2–4 beyond
  the faders and CH3 CUE; SHIFT variants of most deck controls.
* Whether BEAT SYNC is sent on press or on release (the capture has both events
  but no timestamps).
