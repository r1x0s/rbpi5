/*
 * map_grv6.c -- the AlphaTheta DDJ-GRV6 control surface.
 *
 * This fork's target: an XDJ-RX3 player (rbp) on a Raspberry Pi 5, driven from a
 * DDJ-GRV6. It is a sibling of map_flx4.c and map_jp21.c, not a replacement:
 * MIDI_MAP selects between them and all of them stay in the build.
 *
 * ============================== PROVENANCE ================================
 *
 * Every note, CC and channel below is MEASURED on the unit: the 2026-10-13
 * capture (tools/grv6-capture.sh, 68 labelled blocks, raw `aseqdump` output),
 * written up control by control in docs/19-grv6-midi.md. No vendor MIDI list
 * for the GRV6 was found, so -- the reverse of map_flx4.c's history -- the
 * capture is the only source, and the FLX4 is used only as a comparison.
 *
 * What "measured" covers, and what it does not:
 *
 *   - MEASURED: deck 1 completely, deck 2's PLAY and jog ring, the mixer on
 *     CH1 (TRIM/EQ/CFX/fader/CUE), all four channel faders, every mixer and
 *     browse control on ch 6, the whole Beat FX section, the pad modes and
 *     pads on deck 1 (all four unshifted bases), deck 2's HOT CUE pad 1, and
 *     the SHIFT variants of browse push and both LOADs.
 *   - EXPECTED, marked `TODO: unverified` where it is used: the rest of deck 2
 *     mirroring deck 1 on ch 1 (it does on the FLX4, and PLAY/jog/pad do here),
 *     CH2's TRIM/EQ on ch 1 and its Sound Color FX knob on CC 24, the BEAT LOOP
 *     pad base 96, and the jog's non-vinyl CC 35 (bound because the FLX4 has
 *     it; it is harmless if the GRV6 never sends it).
 *   - NOT MEASURED AT ALL: every LED, the meters, whether the unit wants a
 *     keepalive. Those are the next bench rows (docs/19 "Not measured yet").
 *
 * Channels are 0-based snd_seq channels, as `aseqdump` prints them -- the same
 * numbers the capture shows, so there is no 1-based conversion anywhere here.
 *
 * ========================= what differs from the FLX4 =====================
 *
 * The GRV6 is mostly an FLX4 wire-for-wire, which is why this file has the
 * FLX4's shape. The differences that change the map:
 *
 *   1. DECKS 3/4. The GRV6 is a 4-deck surface: the DECK button (note 114)
 *      moves a side to ch 2/3, and note 60 On/Off says which layer is live.
 *      rbp has two players, so while a 3/4 layer is live its controls are
 *      ignored and logged (decision D6). The decks' own channels 0/1 are
 *      simply silent in that time, so nothing has to be suppressed.
 *   2. THE BEAT FX KNOB is a 14-position switch that sends ONE NOTE PER
 *      POSITION (32..45), not the FLX4's FX SELECT step button. So the map
 *      sends rbp an absolute position and keeps no cursor (D18 for the three
 *      effects rbp does not have).
 *   3. THE FX CHANNEL is six radio buttons on ch 4 (1, 2, 3, 4, SP, MST), not a
 *      two-leg lever. The Note Off of a press is the DESELECT of the previous
 *      button, not a release, so only Note On means anything (D17).
 *   4. BEAT SYNC has no separate long-press note (FLX4: 92); SHIFT + LOAD is
 *      88/89 (FLX4: 104/122); SHIFT + PLAY is 71 (FLX4: 14).
 *   5. New controls: BACK, VIEW (-> BROWSE), PREVIEW, DISCOVER (-> SOURCE)
 *      and a four-way browse tilt;
 *      KEY SYNC, MASTER TEMPO and MEMORY on each deck; BOOTH LEVEL.
 *   6. MASTER CUE IS DONE IN THE CONTROLLER. Measured during the audio check:
 *      pressing it mixes master into the headphones with no host involved. So
 *      this map does not toggle rbp's own master cue from the button, and holds
 *      rbp's off -- see grv6_master_cue_hold() for why both halves are needed.
 *   7. The jog has ~9x the FLX4's resolution (6448.8 counts/rev against 720),
 *      and sends ~3.5 counts per message, which changes how its speed has to be
 *      measured -- see grv6_jog().
 *
 * Everything about rbp lives in rbp_abi.h/rbp_bridge.h; the only raw addresses
 * here are grv6_startup()'s two mixer-route words, named in rbp_abi.h.
 */
#define _GNU_SOURCE
#include <stdint.h>         /* uint32_t, for the two mixer-route words */
#include <time.h>           /* clock_gettime, for the jog's microsecond clock */
#include <sound/asequencer.h>

#include "shimutil.h"
#include "shmstate.h"       /* g_cue_mix / g_cue_gain, which the knobs feed */
#include "rbp_abi.h"
#include "rbp_bridge.h"
#include "rbp_vu.h"         /* g_fader / g_fader_seen: the meter scaling input */
#include "ctrl_map.h"

/* ---- receive channels (0-based, as aseqdump prints them) ----------------- */
#define CH_DECK1  0   /* deck 1: buttons, jog, tempo, its mixer strip */
#define CH_DECK2  1   /* deck 2: the same set (beyond PLAY/jog/pad: expected) */
#define CH_DECK3  2   /* deck 3 layer of the left side  -- ignored (D6) */
#define CH_DECK4  3   /* deck 4 layer of the right side -- ignored (D6) */
#define CH_FX     4   /* the whole Beat FX section */
#define CH_MIX    6   /* mixer knobs, browse, LOAD, MASTER CUE, browse buttons */
#define CH_PADS1  7   /* deck 1 pads */
#define CH_PADS1S 8   /* deck 1 pads + SHIFT */
#define CH_PADS2  9   /* deck 2 pads */
#define CH_PADS2S 10  /* deck 2 pads + SHIFT */

/* ---- deck buttons (on the deck's channel) -------------------------------- */
#define N_PLAY        11
#define N_CUE         12
#define N_SHIFT       63  /* log-only: every SHIFT variant is its own note */
#define N_JOG_TOUCH   54  /* top of the platter (+SHIFT 103, unbound) */
#define N_IN          16
#define N_OUT         17
#define N_4BEAT       77  /* 4 BEAT / EXIT -> rbp's RELOOP/EXIT */
#define N_CALL_PREV   81  /* CUE/LOOP CALL < -- rbp keycode unidentified */
#define N_CALL_NEXT   83  /* CUE/LOOP CALL > -- ditto */
#define N_SYNC        88  /* BEAT SYNC; short and long press alike */
#define N_CH_CUE      84  /* channel CUE -> rbp's mixer engine, latching */
#define N_KEY_SYNC    101 /* KEY SYNC -- rbp has no such keycode */
#define N_MASTER_TEMPO 26 /* MASTER TEMPO -> rbp's K_MT (key lock) */
#define N_MEMORY      61  /* MEMORY -- rbp keycode unidentified */
#define N_DECK_SEL    114 /* DECK button, on the TARGET deck's channel */
#define N_LAYER       60  /* layer-active state: On = this channel's layer is live */

/* Pad-mode buttons. Bound BY NAME (D19), the owner's choice for this unit, and
 * unlike map_flx4.c's positional binding: rbp's four modes are 0 HOT CUE,
 * 1 AUTO BEAT LOOP, 2 SLIP BEAT LOOP, 3 BEAT JUMP, and the GRV6 prints HOT CUE,
 * STEMS, BEAT JUMP, SAMPLER (and with SHIFT: KEYBOARD, PAD FX, BEAT LOOP, KEY
 * SHIFT). The two names that match go to their own mode, BEAT LOOP goes to AUTO
 * BEAT LOOP, and SAMPLER -- the one spare unshifted button -- carries SLIP BEAT
 * LOOP, the one mode left over. STEMS, KEYBOARD, PAD FX and KEY SHIFT reach
 * nothing: rbp has no such modes. */
#define N_MODE_HOT     27  /* HOT CUE          -> rbp HOT CUE        (0) */
#define N_MODE_STEMS   30  /* STEMS            -> nothing                */
#define N_MODE_JUMP    32  /* BEAT JUMP        -> rbp BEAT JUMP      (3) */
#define N_MODE_SMPL    34  /* SAMPLER          -> rbp SLIP BEAT LOOP (2) */
#define N_MODE_KBD     105 /* SHIFT+HOT CUE:   KEYBOARD  -> nothing      */
#define N_MODE_PADFX   107 /* SHIFT+STEMS:     PAD FX    -> nothing      */
#define N_MODE_BLOOP   109 /* SHIFT+BEAT JUMP: BEAT LOOP -> AUTO BEAT LOOP (1) */
#define N_MODE_KEYSH   111 /* SHIFT+SAMPLER:   KEY SHIFT -> nothing      */

/* The pads' note bases (pad p = base + p). The unit re-addresses its eight pads
 * when a mode button is pressed. MEASURED on deck 1: HOT CUE 0, STEMS 16,
 * BEAT JUMP 32, SAMPLER 48. BEAT LOOP's base 96 is the FLX4's (KEYBOARD 64,
 * PAD FX 80, BEAT LOOP 96, KEY SHIFT 112); on this unit its LED lit at 7/96 in
 * BEAT LOOP mode (C-044) and the pads acted in that mode in G5, which is the
 * input base by the LED rule -- the capture itself never reached it. */
#define PAD_BASE_HOT    0
#define PAD_BASE_STEMS  16   /* unbound: STEMS puts rbp in no mode */
#define PAD_BASE_JUMP   32
#define PAD_BASE_SMPL   48
#define PAD_BASE_BLOOP  96   /* its LED lit at 7/96 in BEAT LOOP mode (C-044) */

/* ---- mixer / browse (ch 6) ------------------------------------------------ */
#define N_MASTER_CUE   99
#define N_BROWSE_PUSH  65
#define N_BROWSE_PUSH_SHIFT 66  /* -> rbp's SOURCE screen, as on the FLX4 */
#define N_TILT_UP      56
#define N_TILT_DOWN    58
#define N_TILT_LEFT    60
#define N_TILT_RIGHT   62
#define N_LOAD1        70
#define N_LOAD2        71
#define N_LOAD1_SHIFT  88   /* -> TRACK FILTER (the FLX4's SHIFT+LOAD 1 action) */
#define N_LOAD2_SHIFT  89   /* -> SHORTCUT     (the FLX4's SHIFT+LOAD 2 action) */
#define N_BACK         101
#define N_VIEW         122  /* -> rbp's BROWSE key: opens/closes the browser (D25) */
#define N_PREVIEW      54   /* no rbp equivalent */
#define N_DISCOVER     53   /* -> rbp's SOURCE screen, the media list (D25) */

/* ---- Beat FX (ch 4) -------------------------------------------------------- */
#define N_FXCH_1       16   /* FX channel buttons: a radio group, Note On only */
#define N_FXCH_2       17
#define N_FXCH_3       18
#define N_FXCH_4       19
#define N_FXCH_MST     20
#define N_FXCH_SP      22
#define N_FX_KNOB_FIRST 32  /* Beat FX knob: one note per position, 32..45 */
#define N_FX_KNOB_LAST  45
#define N_FX_BEATPREV  74
#define N_FX_BEATNEXT  75
#define N_FX_ONOFF     71

/* ---- CCs (14-bit pairs: MSB on n, LSB on n+32; only the MSB is bound except
 * where a handler combines both) -------------------------------------------- */
#define CC_TEMPO_MSB   0
#define CC_TEMPO_LSB   32
#define CC_JOG_WHEEL   33   /* side ring, relative, 0x40-centred */
#define CC_JOG_VINYL   34   /* top of the platter, relative, 0x40-centred */
#define CC_JOG_NOVINYL 35   /* the FLX4's non-vinyl platter; not seen on the GRV6 */
#define CC_JOG_SHIFT   41   /* platter while SHIFT is held */
#define CC_TRIM        4
#define CC_EQH         7
#define CC_EQM         11
#define CC_EQL         15
#define CC_FADER       19
#define CC_XFADER      31
#define CC_CFX1        23   /* Sound Color FX knob, CH1 (measured) */
#define CC_CFX2        24   /* ditto CH2 -- TODO: unverified (the FLX4's number) */
#define CC_HP_MIX      12
#define CC_HP_LEVEL    13
#define CC_MASTER_MSB  8
#define CC_MASTER_LSB  40
#define CC_BOOTH_MSB   9    /* BOOTH LEVEL: hardware-only, deliberately unbound (D20) */
#define CC_FX_DEPTH    2
#define CC_BROWSE      64   /* relative, 0x01 = clockwise / 0x7F = counter-clockwise */
#define CC_BROWSE_SHIFT 100 /* the FLX4's SHIFT + browse turn; not seen on the GRV6 */

/* The HDMI mirror's unity point in the MASTER LEVEL knob's travel; the FLX4's
 * law and default, see map_flx4.c's flx4_mastervol_gain(). */
#define MIRROR_GAIN_MID_DEFAULT 0.6

/* The jog's counts per revolution. MEASURED: ten steady turns of the top gave
 * 64 488 counts, 6448.8/rev (docs/19); one hand turn is ~1 % coarse, so the
 * integer is as precise as the measurement. */
#define GRV6_JOG_PPR 6449

/* The units the jog's POSITION field is sent in. map_flx4.c sends raw platter
 * counts there, at 720 per revolution, and that is what rbp has been measured
 * working with; what rbp does with the field is still an open question (docs/15
 * open question 11). So the GRV6 sends its position in the same units the FLX4
 * does -- 720 per revolution, rescaled from its own counts -- and rbp sees the
 * same field it has already been seen to accept. Speed does not depend on it. */
#define JOG_POS_PPR 720

/* How long the jog accumulates counts before it reports a speed, in ms.
 * 0 = report on every message, as map_flx4.c does. See grv6_jog(). */
#define JOG_WINDOW_MS_DEFAULT 8

/* ---- the map's own calibration (read by value: empty means default) ------ */
static int knob_scale = 1;
static int jog_scale = 1;
static int jog_ppr = GRV6_JOG_PPR;
static int jog_rev = 0;
static int jog_idle_ms = 120;
static int jog_window_ms = JOG_WINDOW_MS_DEFAULT;
static int jog_verbose = 0;
static int tempo_verbose = 0;
static int tempo_rev = 0;
static int master_cue_hw = 1;    /* MASTER_CUE_HW: 1 = the controller owns it */

/* rbp's effect position for each Beat FX knob note, 32..45 in order (D18).
 * rbp's positions (fx_zone.c): 0 DELAY, 1 ECHO, 2 PING PONG, 3 SPIRAL, 4 HELIX,
 * 5 REVERB, 6 FLANGER, 7 PHASER, 8 FILTER, 9 TRANS, 10 ROLL, 11 SLIP ROLL,
 * 12 PITCH, 13 VINYL BRAKE. Eleven GRV6 effects match by name; the three that
 * do not (LC ECHO, both MOBIUS) take the three rbp effects no GRV6 position
 * names, in knob order -- so every rbp effect is reachable and every click
 * moves somewhere. */
static const int fx_knob_pos[N_FX_KNOB_LAST - N_FX_KNOB_FIRST + 1] = {
      0,   /* 32 DELAY            -> DELAY       */
      1,   /* 33 ECHO             -> ECHO        */
      2,   /* 34 LC ECHO          -> PING PONG   (no LC ECHO in rbp)   */
      3,   /* 35 SPIRAL           -> SPIRAL      */
      4,   /* 36 HELIX            -> HELIX       */
      5,   /* 37 REVERB           -> REVERB      */
      6,   /* 38 FLANGER          -> FLANGER     */
      7,   /* 39 PHASER           -> PHASER      */
      8,   /* 40 FILTER           -> FILTER      */
      9,   /* 41 TRANS            -> TRANS       */
     12,   /* 42 PITCH            -> PITCH       */
     10,   /* 43 ROLL             -> ROLL        */
     11,   /* 44 MOBIUS (saw)     -> SLIP ROLL   (no MOBIUS in rbp)    */
     13,   /* 45 MOBIUS (tri)     -> VINYL BRAKE (no MOBIUS in rbp)    */
};

/* ---- the jog -------------------------------------------------------------- */
struct jog_ctrl {
     int rch;
     int sch;
     int started;                /* a motion is in progress */
     unsigned long long last_ms; /* the last message, for the idle edge */
     unsigned long long win_us;  /* start of the current speed window */
     long pending;               /* counts in the current window */
     long long acc;              /* every count since start, x jog_scale */
     unsigned int vpos;          /* acc in JOG_POS_PPR units, 16-bit wrapping */
     float speed;
     int moving;                 /* a non-zero speed has been sent */
};
static struct jog_ctrl jog_state[2] = {
     { CH_DECK1, 1, 0, 0, 0, 0, 0, 0, 0.0f, 0 },
     { CH_DECK2, 2, 0, 0, 0, 0, 0, 0, 0.0f, 0 },
};

/* ---- the pitch fader (the FLX4's handler, unchanged) ---------------------- */
struct pitch_ctrl {
     int rch;
     int sch;
     int have_hi;
     int pos;                    /* 14-bit */
};
static struct pitch_ctrl pitch_state[2] = {
     { CH_DECK1, 1, 0, 0x2000 },
     { CH_DECK2, 2, 0, 0x2000 },
};

/* ---- MASTER LEVEL ---------------------------------------------------------- */
static int mastervol_have_hi = 0;
static int mastervol_pos = 0;
static double mastervol_mid = MIRROR_GAIN_MID_DEFAULT;

static unsigned long long now_us(void)
{
     struct timespec ts;
     clock_gettime(CLOCK_MONOTONIC, &ts);
     return (unsigned long long)ts.tv_sec * 1000000ULL +
            (unsigned long long)ts.tv_nsec / 1000ULL;
}

/* Floor division, so the position is continuous through 0 when the platter
 * turns back past where it started (C's `/` truncates toward zero, which would
 * make the bucket at 0 twice as wide as every other). */
static long long floordiv(long long a, long long b)
{
     long long q = a / b;
     if ((a % b != 0) && ((a < 0) != (b < 0)))
          q--;
     return q;
}

/* ---- the note dispatch ---------------------------------------------------- */

static void grv6_log_only(int ch, int note)
{
     if (verbose)
          klog("knobshim2: ch%d note%d (log-only)\n", ch, note);
}

static void grv6_note(int ch, int note, int on)
{
     /* Decks 3/4 (D6): rbp has two players, so a 3/4 layer reaches nothing.
      * Logged rather than silently dropped, so a dump of a "dead" deck says
      * why. The layer switch itself arrives on 0/1 too (notes 114 and 60) and
      * is a log-only table row there. */
     if (ch == CH_DECK3 || ch == CH_DECK4) {
          if (verbose)
               klog("knobshim2: deck %d layer ch%d note%d on=%d: ignored "
                    "(rbp has two players)\n", ch + 1, ch, note, on);
          return;
     }

     /* CH CUE: rbp's mixer engine, latching from the engine's own state --
      * map_flx4.c's handler. The GRV6 cannot do this in hardware: it receives
      * only master and headphones from the host, never the channels. */
     if ((ch == CH_DECK1 || ch == CH_DECK2) && note == N_CH_CUE) {
          if (on) {
               int cur = me_get_cue(ch);
               int want = (cur == 0) ? 1 : 0;
               me_set_cue(ch, want);
               if (verbose)
                    klog("knobshim2: CH CUE ch%d -> cue=%d\n", ch + 1, want);
          }
          return;
     }

     /* MASTER CUE: the controller does it (see the header, item 6), so under
      * MASTER_CUE_HW=1 the button is the controller's alone and rbp's own
      * master cue is held off in tick(). MASTER_CUE_HW=0 is map_flx4.c's
      * behaviour, kept as the no-rebuild way back. */
     if (ch == CH_MIX && note == N_MASTER_CUE) {
          if (on) {
               if (master_cue_hw) {
                    klog("knobshim2: MASTER CUE pressed: handled by the "
                         "controller (MASTER_CUE_HW=1)\n");
               } else {
                    int cur = me_get_master_cue();
                    int want = (cur == 0) ? 1 : 0;
                    me_set_master_cue(want);
                    klog("knobshim2: master cue -> %s\n", want ? "ON" : "OFF");
               }
          }
          return;
     }

     /* BEAT SYNC: one gesture per press, on whichever edge arrives first --
      * map_flx4.c's handler, kept because whether the GRV6 sends on press or
      * on release is not measured (the capture has both events and no clock).
      * The GRV6 has no long-press note, so rbp's MASTER (the FLX4's note 92)
      * is not reachable from this button. */
     if ((ch == CH_DECK1 || ch == CH_DECK2) && note == N_SYNC) {
          static int sync_down[2];
          int d = ch;
          if (on) {
               if (sync_down[d])
                    return;
               sync_down[d] = 1;
          } else if (sync_down[d]) {
               sync_down[d] = 0;
               return;
          }
          send_rx_key(K_SYNC, OP_PRESS, ch + 1, 0);
          send_rx_key(K_SYNC, OP_RELEASE, ch + 1, 0);
          if (verbose)
               klog("knobshim2: BEAT SYNC ch%d -> 0x%04x press+release\n",
                    ch + 1, K_SYNC);
          return;
     }

     /* FX channel buttons (D17). A radio group: a press sends Note On for the
      * new button and Note Off for the OLD one, so Note Off is a deselect and
      * is ignored. rbp's Beat FX has one target: 1 and 2 are its players, MST
      * is MASTER; 3, 4 and SP have no rbp target and change nothing. */
     if (ch == CH_FX && note >= N_FXCH_1 && note <= N_FXCH_SP &&
         note != 21) {
          int want;
          if (!on)
               return;
          if (note == N_FXCH_1)
               want = 0;
          else if (note == N_FXCH_2)
               want = 1;
          else if (note == N_FXCH_MST)
               want = BFX_CH_MASTER;
          else {
               klog("knobshim2: FX CH %s: no rbp target, Beat FX target "
                    "unchanged\n",
                    note == N_FXCH_3 ? "3" : note == N_FXCH_4 ? "4" : "SP");
               return;
          }
          send_rx_key(K_BFXCH, OP_VALUE, CH_GLOBAL, want);
          klog("knobshim2: FX CH -> Beat FX target %s\n",
               want == BFX_CH_MASTER ? "MASTER (5)" :
               (want == 0 ? "deck 1 (0)" : "deck 2 (1)"));
          return;
     }

     /* The Beat FX knob (D18): an absolute position per note, Note On only
      * (each click also sends a Note Off, which carries nothing new). */
     if (ch == CH_FX && note >= N_FX_KNOB_FIRST && note <= N_FX_KNOB_LAST) {
          if (on) {
               int pos = fx_knob_pos[note - N_FX_KNOB_FIRST];
               send_rx_key(K_BFXTYPE, OP_VALUE, CH_GLOBAL, pos);
               klog("knobshim2: Beat FX knob note %d -> type position %d\n",
                    note, pos);
          }
          return;
     }

     /* BEAT < / > : PRESS only, as map_flx4.c explains (asEventCode gates
      * 0x4490/0x4491 on op 0). */
     if (ch == CH_FX && (note == N_FX_BEATPREV || note == N_FX_BEATNEXT)) {
          if (on) {
               int key = (note == N_FX_BEATPREV) ? K_BEATPREV : K_BEATNEXT;
               long d = (note == N_FX_BEATPREV) ? -1 : 1;
               send_rx_key_fl(key, OP_PRESS, CH_GLOBAL, d, 0.0f, d);
          }
          return;
     }

     /* Browse tilt UP / DOWN: one step of the list, the browse knob's own
      * keycode -- and the keyboard map's sign (map_kbd.c: UP = -1, DOWN = +1,
      * which the operator confirmed on a list). LEFT/RIGHT are table rows. */
     if (ch == CH_MIX && (note == N_TILT_UP || note == N_TILT_DOWN)) {
          if (on) {
               int d = (note == N_TILT_UP) ? -1 : 1;
               send_rx_key(K_SELECTOR, OP_ROTATE, CH_GLOBAL, d);
               if (verbose)
                    klog("knobshim2: browse tilt %s -> 0x%04x rotate %d\n",
                         d < 0 ? "UP" : "DOWN", K_SELECTOR, d);
          }
          return;
     }

     /* SHIFT + a HOT CUE pad: delete that pad's cue -- map_flx4.c's gesture.
      * MEASURED here too: SHIFT + pad 1 in HOT CUE arrives as ch 8 note 0. */
     if ((ch == CH_PADS1S || ch == CH_PADS2S) && note >= 0 && note < 8) {
          if (on)
               hotcue_delete(ch == CH_PADS1S ? 0 : 1, note + 1);
          return;
     }

     /* Everything else is a table row. */
     for (int i = 0; i < note_map_n; i++) {
          if (note_map[i].rch != ch || note_map[i].note != note)
               continue;
          int key = note_map[i].key;
          if (key == 0) {
               grv6_log_only(ch, note);
               return;
          }
          int *p = &note_map[i].pressed;
          if (on && !*p) {
               *p = 1;
               send_rx_key(key, OP_PRESS, note_map[i].sch, 0);
               if (note_map[i].repeat)
                    send_rx_key(key, OP_REPEAT, note_map[i].sch, 0);
               if (verbose)
                    klog("knobshim2: ch%d note%d -> 0x%04x press%s (sch%d)\n",
                         ch, note, key, note_map[i].repeat ? "+repeat" : "",
                         note_map[i].sch);
          } else if (!on && *p) {
               *p = 0;
               send_rx_key(key, OP_RELEASE, note_map[i].sch, 0);
               if (verbose)
                    klog("knobshim2: ch%d note%d -> 0x%04x release (sch%d)\n",
                         ch, note, key, note_map[i].sch);
          }
          return;
     }
     if (verbose)
          klog("knobshim2: unmapped ch%d note%d on=%d\n", ch, note, on);
}

/* ---- CC handlers ---------------------------------------------------------- */

static void grv6_cc_abs(int ch, int cc, int val)
{
     for (int i = 0; i < abs_map_n; i++) {
          if (abs_map[i].rch != ch || abs_map[i].cc != cc)
               continue;
          int v = cc_to_10bit(val);
          if (v == abs_map[i].last)
               return;
          abs_map[i].last = v;
          int key = abs_map[i].key;
          int sch = abs_map[i].sch;
          float fval = (float)v / 1023.0f;
          if (key == K_FADER && sch >= 1 && sch <= 2) {
               g_fader[sch] = v;
               g_fader_seen[sch] = 1;
          } else if (key == K_HPMIX) {
               g_cue_mix = fval;
          } else if (key == K_HPLEVEL) {
               g_cue_gain = fval;
          }
          send_rx_key_f(key, OP_VALUE, sch, v, fval);
          if (verbose)
               klog("knobshim2: ch%d cc%d -> 0x%04x val=%d f=%.3f (sch%d)\n",
                    ch, cc, key, v, (double)fval, sch);
          return;
     }
     if (verbose)
          klog("knobshim2: unmapped ch%d cc%d val=%d\n", ch, cc, val);
}

/* BROWSE: two's-complement delta, 0x01 clockwise / 0x7F counter-clockwise --
 * measured (3 clicks each way: 1,1,1,127,127,127). */
static void grv6_browse(int val)
{
     if (val < 0 || val > 127)
          return;
     int delta = (val >= 64) ? (val - 128) : val;
     if (delta == 0)
          return;
     long n = (long)delta * (long)knob_scale;
     send_rx_key(K_SELECTOR, OP_ROTATE, CH_GLOBAL, n < 0 ? -1 : 1);
     if (verbose)
          klog("knobshim2: browse knob v=%d delta=%ld -> 0x%04x rotate\n",
               val, n, K_SELECTOR);
}

/* The jog. rbp's contract is map_flx4.c's: key 0x4305, OP_ROTATE, f = rev/s,
 * l = a 16-bit position. What differs is how the speed is measured.
 *
 * The FLX4 reports speed per message, dividing that message's count by the
 * milliseconds since the previous one. On the GRV6 that does not work: one
 * steady revolution is ~1 800 messages of ~3.5 counts (block [10] of the
 * capture: 18 219 messages over ten turns), so at a normal turning speed most
 * messages arrive less than a millisecond apart. A millisecond clock then reads
 * 0 or 1 ms for the same motion, and the reported speed jumps by 2x from one
 * message to the next.
 *
 * So counts are ACCUMULATED over a short window (JOG_WINDOW_MS, default 8 ms)
 * on a microsecond clock, and one speed is reported per window: at one rev/s
 * that is ~50 counts over a precisely known 8 ms, and rbp hears 125 updates a
 * second instead of ~1 800. The position is updated on every message regardless
 * and rides along on the next report. JOG_WINDOW_MS=0 is the FLX4's
 * per-message behaviour.
 *
 * A motion "starts" on the first message after JOG_IDLE_MS of silence: its
 * window opens there, so the first report measures the motion and not the
 * silence before it.
 *
 * Shared with tick() exactly as map_flx4.c's jog state is: the idle edge reads
 * what this writes, from another thread, with no lock. The worst case is one
 * extra speed-0 message at the end of a motion. */
static void grv6_jog(int ch, int val)
{
     int idx = (ch == CH_DECK2) ? 1 : 0;
     struct jog_ctrl *s = &jog_state[idx];
     int d = val - 0x40;
     if (d == 0)
          return;
     unsigned long long t = shim_now_ms();
     unsigned long long tu = now_us();
     if (!s->started ||
         (unsigned long long)(t - s->last_ms) >= (unsigned long long)jog_idle_ms) {
          s->started = 1;
          s->win_us = tu;
          s->pending = 0;
     }
     s->last_ms = t;
     if (jog_rev)
          d = -d;
     long dp = (long)d * jog_scale;
     s->acc += dp;
     s->vpos = (unsigned int)floordiv(s->acc * JOG_POS_PPR,
                                      (long long)jog_ppr) & 0xFFFFu;
     s->pending += dp;

     unsigned long long el = tu - s->win_us;
     if (el == 0 || el < (unsigned long long)jog_window_ms * 1000ULL)
          return;
     float dt = (float)el / 1000000.0f;
     float speed = (float)s->pending / (float)(jog_ppr * jog_scale) / dt;
     if (speed > 8.0f) speed = 8.0f;
     if (speed < -8.0f) speed = -8.0f;
     s->pending = 0;
     s->win_us = tu;
     s->moving = 1;
     s->speed = speed;
     send_rx_key_fl(K_JOG_ROT, OP_ROTATE, s->sch, 0, speed, (long)s->vpos);
     if (jog_verbose)
          klog("knobshim2: jog ch%d speed=%.3f pos=%u window=%lluus (sch%d)\n",
               ch, (double)speed, s->vpos, el, s->sch);
}

/* The end-of-motion edge: speed 0 once the wheel has been still for
 * JOG_IDLE_MS. Also sent for a motion too short to have filled one window, so
 * its position reaches rbp. */
static void grv6_jog_idle(void)
{
     unsigned long long t = shim_now_ms();
     for (int i = 0; i < 2; i++) {
          struct jog_ctrl *s = &jog_state[i];
          if (!s->started || (!s->moving && s->pending == 0))
               continue;
          if ((unsigned long long)(t - s->last_ms) <
              (unsigned long long)jog_idle_ms)
               continue;
          s->moving = 0;
          s->pending = 0;
          s->speed = 0.0f;
          send_rx_key_fl(K_JOG_ROT, OP_ROTATE, s->sch, 0, 0.0f, (long)s->vpos);
          if (jog_verbose)
               klog("knobshim2: jog ch%d idle -> speed 0\n", s->rch);
     }
}

/* The pitch fader: map_flx4.c's flx4_pitch(), unchanged. Which end of the GRV6's
 * fader is "+" is TODO: unverified (the capture swept end to end without saying
 * which end came first); TEMPO_REV flips it without a rebuild. */
static void grv6_pitch(int ch, int cc, int val)
{
     int idx = (ch == CH_DECK2) ? 1 : 0;
     struct pitch_ctrl *s = &pitch_state[idx];
     if (cc == CC_TEMPO_MSB) {
          s->have_hi = 1;
          s->pos = (s->pos & 0x7f) | (val << 7);
          return;
     }
     if (cc != CC_TEMPO_LSB)
          return;
     s->pos = (s->pos & 0x3f80) | (val & 0x7f);
     if (!s->have_hi)
          return;
     int pos = s->pos;
     if (pos < 0) pos = 0;
     if (pos > 0x3FFF) pos = 0x3FFF;
     float norm = ((float)pos - 0x2000) / 8192.0f;
     if (norm > 1.0f) norm = 1.0f;
     if (norm < -1.0f) norm = -1.0f;
     if (tempo_rev)
          norm = -norm;
     int v10 = (int)((norm + 1.0f) * 511.5f);
     if (v10 < 0) v10 = 0;
     if (v10 > 1023) v10 = 1023;
     send_rx_key_fl(K_TEMPO_SLIDER, OP_VALUE, s->sch, (long)v10, norm, (long)pos);
     if (tempo_verbose)
          klog("knobshim2: pitch ch%d (deck %d) pos=0x%04x -> norm=%.3f v10=%d\n",
               ch, s->sch, pos, (double)norm, v10);
}

/* MASTER LEVEL -> the HDMI mirror only: map_flx4.c's law and reasoning. The
 * knob is the controller's own output volume, downstream of the USB audio, so
 * driving rbp's master from it would attenuate the master twice.
 * TODO: unverified on this unit -- that the GRV6's MASTER LEVEL acts on its
 * analogue output the way the FLX4's does (the audio rows G2/G3 settle it). */
static float grv6_mastervol_gain(int pos)
{
     double mid = mastervol_mid;
     double g;

     if (!(mid > 0.0) || mid > 1.0)
          mid = MIRROR_GAIN_MID_DEFAULT;
     g = ((double)pos / 16383.0) / mid;
     if (g > 1.0)
          g = 1.0;
     return (float)g;
}

static void grv6_mastervol(int cc, int val)
{
     if (cc == CC_MASTER_MSB) {
          mastervol_have_hi = 1;
          mastervol_pos = (mastervol_pos & 0x7f) | (val << 7);
          return;
     }
     mastervol_pos = (mastervol_pos & 0x3f80) | (val & 0x7f);
     if (!mastervol_have_hi)
          return;
     int pos = mastervol_pos;
     if (pos < 0) pos = 0;
     if (pos > 0x3FFF) pos = 0x3FFF;
     g_mirror_gain = grv6_mastervol_gain(pos);
}

/* ---- the tables ------------------------------------------------------------ */

static void grv6_build(void)
{
     /* ADDITIVE, like every build(): see ctrl_bindings_reset() in ctrl_map.h. */
     knob_scale = env_num("KNOB_SCALE", 1);
     if (knob_scale < 1) knob_scale = 1;
     jog_scale = env_num("JOG_SCALE", 1);
     if (jog_scale < 1) jog_scale = 1;
     jog_ppr = env_num("JOG_PPR", GRV6_JOG_PPR);
     if (jog_ppr < 1) jog_ppr = 1;
     jog_rev = env_on("JOG_REV", 0);
     jog_idle_ms = env_num("JOG_IDLE_MS", 120);
     if (jog_idle_ms < 10) jog_idle_ms = 10;
     jog_window_ms = env_num("JOG_WINDOW_MS", JOG_WINDOW_MS_DEFAULT);
     if (jog_window_ms < 0) jog_window_ms = 0;
     if (jog_window_ms > 50) jog_window_ms = 50;   /* more is latency, not accuracy */
     mastervol_mid = env_dnum("MIRROR_GAIN_MID", MIRROR_GAIN_MID_DEFAULT);
     jog_verbose = env_on("JOG_VERBOSE", 0);
     tempo_verbose = env_on("TEMPO_VERBOSE", 0);
     tempo_rev = env_on("TEMPO_REV", 0);
     master_cue_hw = env_on("MASTER_CUE_HW", 1);

     /* ---- decks 1/2 (ch 0/1 -> rbp players 1/2) ---- */
     for (int d = 0; d < 2; d++) {
          int rch = CH_DECK1 + d;
          int sch = 1 + d;
          add_note(rch, N_PLAY, K_PLAY, sch);
          add_note(rch, N_CUE, K_CUE, sch);
          add_note(rch, N_JOG_TOUCH, K_JOG_TOUCH, sch);
          add_note(rch, N_IN, K_LOOPIN, sch);
          add_note(rch, N_OUT, K_LOOPOUT, sch);
          add_note(rch, N_4BEAT, K_RELOOP, sch);
          /* MASTER TEMPO is rbp's key lock: a direct name match. */
          add_note(rch, N_MASTER_TEMPO, K_MT, sch);

          /* The pad modes, by name (D19 -- see the constants). */
          add_note(rch, N_MODE_HOT, K_HOTCUE, sch);
          add_note(rch, N_MODE_JUMP, K_BEATJUMP, sch);
          add_note(rch, N_MODE_SMPL, K_SLIPLOOP, sch);
          add_note(rch, N_MODE_BLOOP, K_ALOOP, sch);
          add_note(rch, N_MODE_STEMS, 0, sch);
          add_note(rch, N_MODE_KBD, 0, sch);
          add_note(rch, N_MODE_PADFX, 0, sch);
          add_note(rch, N_MODE_KEYSH, 0, sch);

          /* Known, deliberately not routed (key 0). CUE/LOOP CALL and MEMORY:
           * rbp's keycodes for them are not identified (map_flx4.c borrowed
           * CALL < for BACK because the FLX4 has no BACK; the GRV6 has one, so
           * CALL < is left for its real keycode). KEY SYNC: rbp has none. The
           * DECK button and the layer state: D6. */
          add_note(rch, N_SHIFT, 0, sch);
          add_note(rch, N_CALL_PREV, 0, sch);
          add_note(rch, N_CALL_NEXT, 0, sch);
          add_note(rch, N_MEMORY, 0, sch);
          add_note(rch, N_KEY_SYNC, 0, sch);
          add_note(rch, N_DECK_SEL, 0, sch);
          add_note(rch, N_LAYER, 0, sch);

          /* The channel strip (CH1 measured; CH2 TODO: unverified). */
          add_abs(rch, CC_TRIM, K_TRIM, sch);
          add_abs(rch, CC_EQH, K_EQH, sch);
          add_abs(rch, CC_EQM, K_EQM, sch);
          add_abs(rch, CC_EQL, K_EQL, sch);
          add_abs(rch, CC_FADER, K_FADER, sch);
     }

     /* ---- pads (ch 7/9) ----
      * A base is bound iff a mode button puts rbp in a mode for it (D19): HOT
      * CUE, BEAT JUMP, SAMPLER (-> SLIP BEAT LOOP) and BEAT LOOP (-> AUTO BEAT
      * LOOP). STEMS' base 16 stays unbound -- that button puts rbp in no mode,
      * so its pads would act in whatever mode rbp was left in. */
     for (int d = 0; d < 2; d++) {
          int rch = CH_PADS1 + 2 * d;
          int sch = 1 + d;
          for (int p = 0; p < 8; p++) {
               add_note(rch, PAD_BASE_HOT + p, K_PAD1 + p, sch);
               add_note(rch, PAD_BASE_JUMP + p, K_PAD1 + p, sch);
               add_note(rch, PAD_BASE_SMPL + p, K_PAD1 + p, sch);
               add_note(rch, PAD_BASE_BLOOP + p, K_PAD1 + p, sch);
          }
     }

     /* ---- mixer / browse (ch 6) ---- */
     add_abs(CH_MIX, CC_CFX1, K_COLOR, 1);
     add_abs(CH_MIX, CC_CFX2, K_COLOR, 2);
     add_abs(CH_MIX, CC_XFADER, K_XFADER, CH_GLOBAL);
     add_abs(CH_MIX, CC_HP_MIX, K_HPMIX, CH_GLOBAL);
     add_abs(CH_MIX, CC_HP_LEVEL, K_HPLEVEL, CH_GLOBAL);
     add_note(CH_MIX, N_BROWSE_PUSH, K_SELECTOR, CH_GLOBAL);
     add_note(CH_MIX, N_BROWSE_PUSH_SHIFT, K_SOURCE, CH_GLOBAL);
     add_note(CH_MIX, N_LOAD1, K_LOAD, 1);
     add_note(CH_MIX, N_LOAD2, K_LOAD, 2);
     /* SHIFT + LOAD: the FLX4's two actions, on the GRV6's notes. TRACK FILTER
      * needs the OP_REPEAT edge (rbp_abi.h); a plain row opens nothing. */
     add_note_repeat(CH_MIX, N_LOAD1_SHIFT, K_TRACKFILTER, CH_GLOBAL);
     add_note(CH_MIX, N_LOAD2_SHIFT, K_SHORTCUT, CH_GLOBAL);
     /* BACK is a real button here. The tilt's LEFT/RIGHT are "out of / into
      * this level", which on rbp is BACK and the browse push -- the keyboard
      * map's BACKSPACE and ENTER. UP/DOWN are dispatched above. */
     add_note(CH_MIX, N_BACK, K_BACK, CH_GLOBAL);
     add_note(CH_MIX, N_TILT_LEFT, K_BACK, CH_GLOBAL);
     add_note(CH_MIX, N_TILT_RIGHT, K_SELECTOR, CH_GLOBAL);
     /* VIEW and DISCOVER (D25, the owner's choice after the first run on the
      * unit): BACK only climbs to the top of the list and never leaves the
      * browser, and rbp's own BROWSE key (the keyboard map's `6`, tried there)
      * is what closes it -- so VIEW, the GRV6's "change the screen" button,
      * sends it. upstream's JP21 map binds its VIEW the same way. DISCOVER
      * opens the SOURCE screen (the USB list) in one press; SHIFT + push still
      * does too. PREVIEW has no rbp equivalent. BOOTH LEVEL (CC 9/41) is not a
      * row at all: it is the controller's own booth output (D20) and arrives as
      * an unmapped CC. */
     add_note(CH_MIX, N_VIEW, K_BROWSE, CH_GLOBAL);
     add_note(CH_MIX, N_PREVIEW, 0, CH_GLOBAL);
     add_note(CH_MIX, N_DISCOVER, K_SOURCE, CH_GLOBAL);

     /* ---- Beat FX (ch 4) ---- */
     add_note(CH_FX, N_FX_ONOFF, K_BFX, CH_GLOBAL);
     add_abs(CH_FX, CC_FX_DEPTH, K_DEPTH, CH_GLOBAL);
}

/* Everything that has to reach rbp once it exists: map_flx4.c's startup, for
 * the same reasons -- route each mixer channel to its player, give the Sound
 * Color FX knobs Filter behind them, and pin rbp's master level at unity because
 * the controller's MASTER LEVEL is the real one. TODO: unverified whether the
 * GRV6 has Sound Color FX select buttons; none was captured, so Filter is a
 * fixed choice here as on the FLX4. */
static void grv6_startup(void)
{
     *(volatile uint32_t *)ADDR_MIXER_ROUTE_PLAYER0 = MIXER_ROUTE_INPUT0;
     *(volatile uint32_t *)ADDR_MIXER_ROUTE_PLAYER1 = MIXER_ROUTE_INPUT1;
     klog("knobshim2: routed Mixer Ch1 -> Deck1, Ch2 -> Deck2\n");

     send_rx_key(K_FILTER, OP_PRESS, 1, 0);
     send_rx_key(K_FILTER, OP_RELEASE, 1, 0);
     send_rx_key(K_FILTER, OP_PRESS, 2, 0);
     send_rx_key(K_FILTER, OP_RELEASE, 2, 0);
     send_rx_key_f(K_COLOR, OP_VALUE, 1, 512, 0.5f);
     send_rx_key_f(K_COLOR, OP_VALUE, 2, 512, 0.5f);
     klog("knobshim2: Sound Color FX initialized to Filter on Ch1 & Ch2\n");

     send_rx_key_f(K_MASTERLVL, OP_VALUE, CH_GLOBAL, 1023, 1.0f);
     klog("knobshim2: master level set to unity\n");
     klog("knobshim2: GRV6: MASTER CUE %s\n", master_cue_hw
          ? "is the controller's (MASTER_CUE_HW=1); rbp's is held off"
          : "toggles rbp's own master cue (MASTER_CUE_HW=0)");
}

/* Hold rbp's own master cue OFF while the controller owns MASTER CUE.
 *
 * Why it has to be held and not just left alone: rbp_vu.c's vu_thread turns
 * rbp's master cue ON once the mixer engine exists, for every surface, because
 * the panels it was written for have no hardware master cue. On the GRV6 that
 * puts master in the headphones permanently, and the button could then only
 * ADD master a second time -- and with map_flx4.c's toggle on top, the two
 * would run in anti-phase and the button would appear dead.
 *
 * Why tick(): vu_thread's write happens once, at a time this map does not
 * control (when rbp's mixer appears), so a one-shot clear from startup() could
 * run before it and be undone. Reading the state every tick and clearing it
 * when it is on settles that race in one tick, logs each clear (so a second
 * writer would show up as repeated lines), and costs one read when it is
 * already off. me_get_master_cue() answers -1 before the engine exists, so
 * nothing is written then. */
static void grv6_master_cue_hold(void)
{
     if (!master_cue_hw)
          return;
     if (me_get_master_cue() == 1) {
          me_set_master_cue(0);
          klog("knobshim2: GRV6: rbp's master cue cleared (the controller's "
               "MASTER CUE does this in hardware)\n");
     }
}

static void grv6_tick(void)
{
     grv6_jog_idle();
     grv6_master_cue_hold();
}

static void grv6_event(const struct snd_seq_event *ev)
{
     switch (ev->type) {
     case SNDRV_SEQ_EVENT_CONTROLLER: {
          int ch = ev->data.control.channel;
          int cc = ev->data.control.param;
          int val = ev->data.control.value;
          if (ch == CH_DECK3 || ch == CH_DECK4) {
               if (verbose)
                    klog("knobshim2: deck %d layer ch%d cc%d: ignored\n",
                         ch + 1, ch, cc);
          } else if ((ch == CH_DECK1 || ch == CH_DECK2) &&
                     (cc == CC_TEMPO_MSB || cc == CC_TEMPO_LSB))
               grv6_pitch(ch, cc, val);
          else if ((ch == CH_DECK1 || ch == CH_DECK2) &&
                   (cc == CC_JOG_WHEEL || cc == CC_JOG_VINYL ||
                    cc == CC_JOG_NOVINYL || cc == CC_JOG_SHIFT))
               grv6_jog(ch, val);
          else if (ch == CH_MIX && (cc == CC_BROWSE || cc == CC_BROWSE_SHIFT))
               grv6_browse(val);
          else if (ch == CH_MIX && (cc == CC_MASTER_MSB || cc == CC_MASTER_LSB))
               grv6_mastervol(cc, val);
          else
               grv6_cc_abs(ch, cc, val);
          break;
     }
     case SNDRV_SEQ_EVENT_NOTEON:
          grv6_note(ev->data.note.channel, ev->data.note.note,
                    ev->data.note.velocity > 0);
          break;
     case SNDRV_SEQ_EVENT_NOTEOFF:
          grv6_note(ev->data.note.channel, ev->data.note.note, 0);
          break;
     default:
          /* Includes the two System Resets the unit sends when a side switches
           * to deck 3/4: nothing in this map's state depends on them. */
          break;
     }
}

/* THE LED TABLE, MEASURED ON THE UNIT 2026-10-13 (C-042's probe, G8): each
 * candidate was streamed from the host (Note On every 100 ms for 3 s, off =
 * velocity 0) and the owner read the panel. The rule the FLX4 taught held again:
 * every LED that lit is its control's own INPUT note on its own input channel.
 *
 * What lit from the host, and so what this table drives:
 *   deck 1 ch 0 / deck 2 ch 1:  PLAY 11 (green), CUE 12 (orange), BEAT SYNC 88
 *     (white), LOOP IN 16 / OUT 17 (orange); deck 2 measured on PLAY and SYNC,
 *     the rest by the same deck_ch + 1 rule. (KEY SYNC 101 lit blue too, but
 *     rbp has no key-sync state to mirror.)
 *   pads: ch 7 (deck 1) and ch 9 (deck 2), notes base + 0..7, base 0 in HOT CUE
 *     and 32 in BEAT JUMP (both seen); BLUE ONLY, and only at velocity 127 --
 *     every velocity from 1 to 120 left the pad dark -- so they are on/off.
 *   CH1 / CH2 CUE: note 84 on ch 0 / 1 (orange).
 *   Beat FX ON/OFF: note 71 on ch 4 -- blue, and the GRV6 BLINKS it itself at
 *     velocity 127 while 0/1 show it solid (C-045), hence fx_panel_blinks.
 *   CH1 meter: CC 2 on ch 0 moved the meter bottom -> top -> bottom.
 *
 * What the GRV6 LIGHTS BY ITSELF, with no host message (owner, same run), and
 * which this table therefore must NOT drive -- two writers would fight:
 *   MASTER TEMPO (red), the DECK 1/3 and 2/4 buttons, the pad mode buttons,
 *   MASTER CUE (whose mix is the controller's too, D21), the FX channel select
 *   buttons, the LOAD buttons. 26, 114, 27/30/32/34, 99/6, 16-22/4 and 70/71/6
 *   were each sent from the host and none lit.
 * No light at all: 4 BEAT/EXIT (77), MEMORY (61), CUE/LOOP CALL < >, BACK, VIEW,
 *   DISCOVER, the SHIFT pad channel (8). The jogs show a spinning line of their
 *   own and nothing from the host (54).
 *
 * The pad bases of the other two modes were probed afterwards (C-044): 7/48 lit
 * pad 1 in SAMPLER (rbp's SLIP BEAT LOOP) and 7/96 lit it in SHIFT + BEAT JUMP =
 * BEAT LOOP (rbp's AUTO BEAT LOOP), so all four bases are seen. */
static const struct led_notes grv6_leds = {
     .deck_ch = 0,             /* MEASURED: PLAY/SYNC lit on ch 0 and ch 1 */
     .n_sync = 88,             /* MEASURED: white */
     .n_cue = 12,              /* MEASURED: orange (the deck CUE) */
     .n_play = 11,             /* MEASURED: green */
     .n_keylock = -1,          /* MASTER TEMPO is lit by the GRV6 itself; 26/0
                                * from the host lit nothing */
     .n_vinyl = -1,            /* no such button */
     .n_slip = -1,             /* no such button */
     .n_loopin = 16,           /* MEASURED: orange */
     .n_loopout = 17,          /* MEASURED: orange */
     .n_autoloop = -1,         /* MEASURED: 77/0 lit nothing; no light */

     .n_mode = { -1, -1, -1, -1 },   /* the GRV6 lights its own mode buttons;
                                      * 27/30/32/34 from the host: nothing */

     .pad_ch = 7,              /* MEASURED: 7/0 and 7/7 lit deck 1 pads 1, 8 */
     .pad_ch2 = 9,             /* MEASURED: 9/0 lit deck 2 pad 1 */
     .n_pad_base = { 0, 96, 48, 32 },   /* rbp modes HOT CUE / AUTO BEAT LOOP /
                                         * SLIP BEAT LOOP / BEAT JUMP, i.e. the
                                         * GRV6's HOT CUE / SHIFT+BEAT JUMP (BEAT
                                         * LOOP) / SAMPLER / BEAT JUMP: all four
                                         * seen lit (48 and 96: C-044) */
     .pad_enc = LED_ENC_NONE,  /* MEASURED: blue at 127 only, 1..120 dark */

     .strip_ch_first = 0,      /* MEASURED: 84/0 = CH1 CUE, 84/1 = CH2 CUE */
     .strip_count = 2,
     .n_strip_cue = 84,

     .master_ch_first = -1,    /* MASTER CUE is the GRV6's own (D21); 99/6 from */
     .master_ch_count = 0,     /* the host lit nothing */
     .n_master_cue = -1,

     .fx_ch = 4,               /* MEASURED: 71/4 lit Beat FX ON/OFF */
     .n_fx = {
          [LED_FX_BFX_ONOFF]   = 71,
          [LED_FX_CFX_FILTER]  = -1,   /* no CFX buttons: one CFX knob per strip */
          [LED_FX_CFX_DUBECHO] = -1,
          [LED_FX_CFX_NOISE]   = -1,
          [LED_FX_CFX_SWEEP]   = -1,
     },

     .meter_ch_first = 0,      /* MEASURED for CH1 (CC 2 on ch 0 moved it); CH2
                                * on ch 1 is EXPECTED by the deck_ch rule */
     .n_meter_cc = 2,
     .meter_enc = METER_ENC_FLX4_LEVEL,   /* EXPECTED: the probe's 0..127 ramp
                                * spanned the meter; the FLX4's 0x26..0x7F scale
                                * is assumed, its bottom threshold unmeasured */
     .meter_pre_fader = 1,     /* EXPECTED (the FLX4's); unmeasured here */
     .meter_master_ch = -1,    /* no master meter probed */
     .n_meter_master_l = -1,
     .n_meter_master_r = -1,

     .fx_panel_blinks = 1,     /* MEASURED (C-045): FX ON/OFF at velocity 0/1 is
                                * solid, at 127 the GRV6 blinks it itself (and
                                * with nothing sent it blinks too). rbp: FX off =
                                * lit, FX on = blink, so off -> 0, on -> 127 */
};

const struct ctrl_map map_grv6 = {
     "grv6",
     grv6_build,
     grv6_startup,
     grv6_event,
     grv6_tick,
     NULL,   /* devices(): a MIDI surface */
     NULL,   /* input(): ditto */
     &grv6_leds,
};
