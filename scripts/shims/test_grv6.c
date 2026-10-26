/*
 * test_grv6.c -- replay tests/midi_grv6.dump through the PRODUCTION GRV6 map.
 *
 * test_flx4.c's discipline, applied to map_grv6.c: the real map, the real
 * binding tables (ctrl_map.o) and the real dump format (mididump.o), with every
 * call that would reach rbp or the panel replaced by a recorder. What it pins:
 *
 *   - the keycode stream the fixture must produce, row by row (expect[]);
 *   - the state the map writes that is not a keycode: the channel-CUE latch,
 *     the headphone gains, the fader inputs of the meter, the HDMI mirror gain,
 *     the SHIFT + HOT CUE deletes -- and that MASTER CUE does NOT touch rbp;
 *   - the master-cue hold in tick(), and the headphone MIX/LEVEL hold beside it
 *     (the knobs themselves no longer reach rbp under MASTER_CUE_HW=1, C-046);
 *   - MASTER_CUE_HW=0, rebuilt: the knobs reach rbp again and nothing is held;
 *   - the jog, driven directly with real sleeps because its speed is measured
 *     against the clock: the sign, the windowing, the idle edge, and the
 *     position in FLX4 units (720/rev) including the step below zero.
 *
 * The fixture's numbers are MEASURED (docs/port/grv6-midi.md) except the lines it
 * marks "expected"; a pass says the map dispatches those numbers the way its
 * tables say, not that the unit agrees about the expected ones.
 *
 * grv6_startup() is not called, for test_flx4.c's reason: it writes two
 * absolute rbp addresses.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sound/asequencer.h>

#include "rbp_abi.h"
#include "rbp_bridge.h"
#include "shmstate.h"
#include "rbp_vu.h"
#include "ctrl_map.h"
#include "map_ux.h"
#include "mididump.h"

#ifndef GRV6_FIXTURE
#error "GRV6_FIXTURE must name the dump to replay (-DGRV6_FIXTURE=...); see the Makefile"
#endif

static int checks, failures;

#define CHECK(cond, ...) do {                                       \
        checks++;                                                   \
        if (!(cond)) {                                              \
            failures++;                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
            printf(__VA_ARGS__);                                    \
            printf("\n");                                           \
        }                                                           \
    } while (0)

/* ---- the globals the map writes, owned by modules not in this link ------- */
int verbose = 0;
int g_fader[3] = { 1023, 1023, 1023 };
int g_fader_seen[3];

/* ---- the recorder --------------------------------------------------------- */
struct sent {
     int kind;        /* 0 = send_rx_key, 1 = _f, 2 = _fl */
     int key, op, ch;
     long param;
     float fval;
     long lval;
};
#define SENT_MAX 512
static struct sent sent[SENT_MAX];
static int sent_n;

static void record(int kind, int key, int op, int ch, long param, float fval,
                   long lval)
{
     if (sent_n >= SENT_MAX) {
          printf("FAIL: more than %d keycodes\n", SENT_MAX);
          exit(1);
     }
     sent[sent_n].kind = kind;
     sent[sent_n].key = key;
     sent[sent_n].op = op;
     sent[sent_n].ch = ch;
     sent[sent_n].param = param;
     sent[sent_n].fval = fval;
     sent[sent_n].lval = lval;
     sent_n++;
}


/* rbp's per-channel Sound Color FX type (rbp_abi.h ME_GET_CFX_TYPE), simulated
 * from the type keys the map sends: a type key on channel 1/2 selects that type
 * there, or switches it off if it is the one selected -- rbp's own rule. */
static int sim_cfx[2];
int me_get_cfx_type(int idx)         { return (idx == 0 || idx == 1) ? sim_cfx[idx] : -1; }
static void sim_cfx_key(int key, int op, int ch)
{
     static const int keys[7] = { 0, K_FILTER, K_NOISE, K_SWEEP, K_DUBECHO, K_SPACE, K_CRUSH };
     if (op != OP_PRESS || ch < 1 || ch > 2)
          return;
     for (int t = 1; t <= 6; t++)
          if (key == keys[t])
               sim_cfx[ch - 1] = (sim_cfx[ch - 1] == t) ? 0 : t;
}

void send_rx_key(int keycode, int op, int ch, long param)
{
     sim_cfx_key(keycode, op, ch);
     record(0, keycode, op, ch, param, 0.0f, 0);
}

void send_rx_key_f(int keycode, int op, int ch, long param, float fval)
{
     record(1, keycode, op, ch, param, fval, 0);
}

void send_rx_key_fl(int keycode, int op, int ch, long param, float fval,
                    long lval)
{
     record(2, keycode, op, ch, param, fval, lval);
}

void *get_key_manager(void)
{
     return (void *)&checks;
}

/* rbp_bridge.c's conversion, copied for test_flx4.c's reason. */
int cc_to_10bit(int v)
{
     if (v < 0) v = 0;
     if (v > 127) v = 127;
     return (v << 3) | (v >> 4);
}

/* ---- rbp state map_ux.c reads (settable, so the checks can move it) ------- */
static int browse_mode = 3;          /* not the performance screen (1) */
int rbp_browse_mode(void)            { return browse_mode; }
int rbp_beatfx_type(void)            { return 7; }   /* PHASER: position 7 */
static int pad_mode[2];              /* 0 HOT CUE .. 3 BEAT JUMP */
int rbp_padmode(int deck)            { return pad_mode[deck ? 1 : 0]; }
static int bfx_led = 1;              /* LedStat 48: 2 = Beat FX on */
int rbp_ledstat_state(unsigned int id, unsigned int ch)
{
     (void)ch;
     return id == 48 ? bfx_led : -1;
}

/* ---- the mixer engine ----------------------------------------------------- */
static int cue_state[2];
static int cue_calls, cue_last = -1, cue_last_ch = -1;
int me_get_cue(int idx)              { return (idx >= 0 && idx < 2) ? cue_state[idx] : -1; }
void me_set_cue(int idx, int on)
{
     cue_calls++;
     cue_last = on;
     cue_last_ch = idx;
     if (idx >= 0 && idx < 2)
          cue_state[idx] = on;
}

/* rbp's master cue, with a real state so the hold can be watched: -1 is "no
 * engine yet", which is what me_get_master_cue() answers before rbp's mixer. */
static int master_cue_state = 0;
static int master_cue_calls, master_cue_last = -1;
int me_get_master_cue(void)          { return master_cue_state; }
void me_set_master_cue(int on)
{
     master_cue_calls++;
     master_cue_last = on;
     master_cue_state = on;
}

static int hcd_calls;
static int hcd_deck[8], hcd_pad[8];
int hotcue_delete(int deck, int pad)
{
     if (hcd_calls < 8) {
          hcd_deck[hcd_calls] = deck;
          hcd_pad[hcd_calls] = pad;
     }
     hcd_calls++;
     return 0;
}

/* ---- the expectation table ------------------------------------------------ */
struct expect {
     int kind, key, op, ch;
     long param;
     float fval;
     long lval;
};

#define F516 (516.0f / 1023.0f)
#define PITCH_NORM(pos) (((float)(pos) - 0x2000) / 8192.0f)
#define PITCH_V10(pos)  ((int)((PITCH_NORM(pos) + 1.0f) * 511.5f))

#define PR(key, ch) { 0, (key), OP_PRESS, (ch), 0, 0.0f, 0 }, \
                    { 0, (key), OP_RELEASE, (ch), 0, 0.0f, 0 }

static const struct expect expect[] = {
     /* ---- deck 1 ---- */
     PR(K_PLAY, 1),
     PR(K_CUE, 1),
     PR(K_SYNC, 1),                  /* one gesture; the release is swallowed */
     /* SHIFT, SHIFT + PLAY: nothing */
     PR(K_MT, 1),                    /* MASTER TEMPO -> key lock */
     /* KEY SYNC: nothing. MEMORY, CALL < and > (2026-10-23): rbp's own */
     PR(K_CUEMEMORY, 1),
     PR(K_CALLPREV, 1),
     PR(K_CALLNEXT, 1),
     PR(K_LOOPIN, 1),
     PR(K_LOOPOUT, 1),
     PR(K_RELOOP, 1),
     PR(K_JOG_TOUCH, 1),
     /* CH CUE x2: the engine, not a keycode */
     /* ---- deck 2 ---- */
     PR(K_PLAY, 2),
     /* ---- deck 3 and back: nothing ---- */
     /* ---- mixer ---- */
     { 1, K_FADER,   OP_VALUE, 1, 0,    0.0f, 0 },
     { 1, K_FADER,   OP_VALUE, 2, 1023, 1.0f, 0 },
     { 1, K_TRIM,    OP_VALUE, 1, 516,  F516, 0 },
     { 1, K_EQH,     OP_VALUE, 1, 516,  F516, 0 },
     { 1, K_EQM,     OP_VALUE, 1, 516,  F516, 0 },
     { 1, K_EQL,     OP_VALUE, 1, 516,  F516, 0 },
     { 1, K_COLOR,   OP_VALUE, 1, 516,  F516, 0 },
     { 1, K_COLOR,   OP_VALUE, 2, 1023, 1.0f, 0 },
     { 1, K_XFADER,  OP_VALUE, CH_GLOBAL, 1023, 1.0f, 0 },
     /* HEADPHONES MIX / LEVEL: the GRV6's own (MASTER_CUE_HW=1, C-046), so no
      * keycode -- rbp's are held by tick() instead, see check_hp_hold() */
     /* BOOTH, MASTER LEVEL, MASTER CUE: no keycode */
     /* ---- browse ---- */
     { 0, K_SELECTOR, OP_ROTATE, CH_GLOBAL, 1,  0.0f, 0 },
     { 0, K_SELECTOR, OP_ROTATE, CH_GLOBAL, -1, 0.0f, 0 },
     PR(K_SELECTOR, CH_GLOBAL),      /* push */
     PR(K_BROWSE, CH_GLOBAL),        /* SHIFT + push: out of the browser (map_ux.c) */
     { 0, K_SELECTOR, OP_ROTATE, CH_GLOBAL, 1,  0.0f, 0 },   /* tilt DOWN */
     { 0, K_SELECTOR, OP_ROTATE, CH_GLOBAL, -1, 0.0f, 0 },   /* tilt UP */
     PR(K_BACK, CH_GLOBAL),          /* tilt LEFT */
     PR(K_SELECTOR, CH_GLOBAL),      /* tilt RIGHT */
     PR(K_LOAD, 1),
     PR(K_LOAD, 2),
     { 0, K_TRACKFILTER, OP_PRESS,   CH_GLOBAL, 0, 0.0f, 0 },
     { 0, K_TRACKFILTER, OP_REPEAT,  CH_GLOBAL, 0, 0.0f, 0 },
     { 0, K_TRACKFILTER, OP_RELEASE, CH_GLOBAL, 0, 0.0f, 0 },
     PR(K_SHORTCUT, CH_GLOBAL),
     PR(K_BACK, CH_GLOBAL),          /* the BACK button */
     PR(K_BROWSE, CH_GLOBAL),        /* VIEW (D25) */
     /* PREVIEW: nothing */
     PR(K_SOURCE, CH_GLOBAL),        /* DISCOVER (D25) */
     /* ---- Beat FX ---- */
     { 0, K_BFXTYPE, OP_VALUE, CH_GLOBAL, 1,  0.0f, 0 },  /* ECHO */
     { 0, K_BFXTYPE, OP_VALUE, CH_GLOBAL, 2,  0.0f, 0 },  /* LC ECHO -> PING PONG */
     { 0, K_BFXTYPE, OP_VALUE, CH_GLOBAL, 12, 0.0f, 0 },  /* PITCH */
     { 0, K_BFXTYPE, OP_VALUE, CH_GLOBAL, 10, 0.0f, 0 },  /* ROLL */
     { 0, K_BFXTYPE, OP_VALUE, CH_GLOBAL, 11, 0.0f, 0 },  /* MOBIUS saw -> SLIP ROLL */
     { 0, K_BFXTYPE, OP_VALUE, CH_GLOBAL, 13, 0.0f, 0 },  /* MOBIUS tri -> VINYL BRAKE */
     { 0, K_BFXTYPE, OP_VALUE, CH_GLOBAL, 0,  0.0f, 0 },  /* DELAY */
     { 0, K_BFXCH,   OP_VALUE, CH_GLOBAL, 0,  0.0f, 0 },  /* FX CH 1 */
     { 0, K_BFXCH,   OP_VALUE, CH_GLOBAL, 1,  0.0f, 0 },  /* FX CH 2 */
     /* 3, 4, SP: nothing */
     { 0, K_BFXCH,   OP_VALUE, CH_GLOBAL, BFX_CH_MASTER, 0.0f, 0 },  /* MST */
     { 2, K_BEATPREV, OP_PRESS, CH_GLOBAL, -1, 0.0f, -1 },
     { 2, K_BEATNEXT, OP_PRESS, CH_GLOBAL, 1,  0.0f, 1 },
     PR(K_BFX, CH_GLOBAL),
     { 1, K_DEPTH,   OP_VALUE, CH_GLOBAL, 1023, 1.0f, 0 },
     /* ---- pads ---- */
     PR(K_HOTCUE, 1),
     PR(K_PAD1, 1),
     PR(K_PAD1 + 7, 1),
     /* STEMS and its pad: nothing */
     PR(K_BEATJUMP, 1),
     PR(K_PAD1 + 1, 1),
     PR(K_SLIPLOOP, 1),              /* SAMPLER */
     PR(K_PAD1 + 7, 1),
     PR(K_ALOOP, 1),                 /* SHIFT + BEAT JUMP = BEAT LOOP */
     PR(K_PAD1, 1),                  /* base 96 */
     /* KEYBOARD, PAD FX, KEY SHIFT, the SHIFT pads: no keycode */
     PR(K_PAD1, 2),
     /* ---- tempo ---- */
     { 2, K_TEMPO_SLIDER, OP_VALUE, 1, PITCH_V10(0x3FFF), PITCH_NORM(0x3FFF), 0x3FFF },
     { 2, K_TEMPO_SLIDER, OP_VALUE, 1, PITCH_V10(0x2000), PITCH_NORM(0x2000), 0x2000 },
};
#define EXPECT_N ((int)(sizeof(expect) / sizeof(expect[0])))

static void dispatch(const struct snd_seq_event *ev)
{
     if (get_key_manager() && map_grv6.event)
          map_grv6.event(ev);
}

static int fixture_events;

/* Count the replayable lines, and check every one re-renders byte-identical. */
static void round_trip(void)
{
     FILE *f = fopen(GRV6_FIXTURE, "r");
     char line[256], buf[256];
     int n_lines = 0;

     CHECK(f != NULL, "cannot open the fixture %s", GRV6_FIXTURE);
     if (!f)
          return;
     while (fgets(line, sizeof line, f)) {
          struct snd_seq_event ev;
          size_t len = strlen(line);
          double t;
          int ty;

          if (len && line[len - 1] == '\n')
               line[--len] = '\0';
          n_lines++;
          if (len == 0 || line[0] == '#')
               continue;
          if (sscanf(line, "%lf SKIP type=%d", &t, &ty) == 2) {
               CHECK(mididump_parse(line, &ev) == 0,
                     "fixture line %d: SKIP parsed as replayable", n_lines);
               continue;
          }
          if (mididump_parse(line, &ev) != 1) {
               CHECK(0, "fixture line %d did not parse: %s", n_lines, line);
               continue;
          }
          fixture_events++;
          CHECK(mididump_format(&ev, (unsigned long long)(t * 1e6 + 0.5),
                                buf, sizeof buf) == (int)len &&
                strcmp(buf, line) == 0,
                "fixture line %d re-rendered as \"%s\"", n_lines, buf);
     }
     fclose(f);
     CHECK(fixture_events > 0, "the fixture carries no replayable events");
}

static void check_stream(void)
{
     CHECK(sent_n == EXPECT_N, "the fixture produced %d keycodes, expected %d",
           sent_n, EXPECT_N);
     for (int i = 0; i < EXPECT_N && i < sent_n; i++) {
          const struct sent *s = &sent[i];
          const struct expect *e = &expect[i];
          CHECK(s->kind == e->kind && s->key == e->key && s->op == e->op &&
                s->ch == e->ch && s->param == e->param &&
                s->fval == e->fval && s->lval == e->lval,
                "keycode %d: got kind%d key=0x%04x op=%d ch=%d param=%ld "
                "f=%.6f l=%ld, expected kind%d key=0x%04x op=%d ch=%d "
                "param=%ld f=%.6f l=%ld",
                i, s->kind, s->key, s->op, s->ch, s->param, (double)s->fval,
                s->lval, e->kind, e->key, e->op, e->ch, e->param,
                (double)e->fval, e->lval);
     }
}

static void check_state(void)
{
     CHECK(g_cue_mix == F516, "g_cue_mix %.4f, expected %.4f",
           (double)g_cue_mix, (double)F516);
     CHECK(g_cue_gain == 0.0f, "g_cue_gain %.4f, expected 0", (double)g_cue_gain);
     CHECK(g_fader[1] == 0 && g_fader_seen[1] == 1,
           "left fader %d (seen %d), expected 0 and seen",
           g_fader[1], g_fader_seen[1]);
     CHECK(g_fader[2] == 1023 && g_fader_seen[2] == 1,
           "right fader %d (seen %d), expected 1023 and seen",
           g_fader[2], g_fader_seen[2]);

     /* MASTER LEVEL raw 8192 of 16383 against unity at 0.6: 0.5/0.6. */
     {
          double want = (8192.0 / 16383.0) / 0.6;
          double got = g_mirror_gain;
          CHECK(got > want - 1e-4 && got < want + 1e-4,
                "MASTER LEVEL left g_mirror_gain %.5f, expected %.5f", got, want);
     }

     CHECK(cue_calls == 3, "CH CUE called me_set_cue() %d times, expected 3",
           cue_calls);
     CHECK(cue_state[0] == 0, "deck 1's cue left %d: the 2nd press is the off "
           "edge", cue_state[0]);
     CHECK(cue_state[1] == 1, "deck 2's cue left %d, expected 1", cue_state[1]);
     CHECK(cue_last_ch == 1 && cue_last == 1,
           "the last me_set_cue() was ch %d -> %d, expected ch 1 -> 1",
           cue_last_ch, cue_last);

     /* The controller owns MASTER CUE: the press must not reach rbp. */
     CHECK(master_cue_calls == 0,
           "MASTER CUE called me_set_master_cue() %d times; under "
           "MASTER_CUE_HW=1 it is the controller's", master_cue_calls);

     CHECK(hcd_calls == 2, "the SHIFT pads called hotcue_delete() %d times, "
           "expected 2", hcd_calls);
     CHECK(hcd_calls != 2 || (hcd_deck[0] == 0 && hcd_pad[0] == 1 &&
                              hcd_deck[1] == 1 && hcd_pad[1] == 4),
           "hotcue_delete(%d,%d) then (%d,%d), expected (0,1) then (1,4)",
           hcd_deck[0], hcd_pad[0], hcd_deck[1], hcd_pad[1]);
}

/* tick(): rbp's master cue is cleared when vu_thread has turned it on, once,
 * and nothing is written while rbp has no mixer (-1). */
static void check_master_cue_hold(void)
{
     int before = master_cue_calls;

     master_cue_state = -1;           /* no engine yet */
     map_grv6.tick();
     CHECK(master_cue_calls == before,
           "tick() wrote rbp's master cue before rbp's mixer existed");

     master_cue_state = 1;            /* vu_thread's startup write */
     map_grv6.tick();
     CHECK(master_cue_calls == before + 1 && master_cue_state == 0,
           "tick() did not clear rbp's master cue (calls %d, state %d)",
           master_cue_calls - before, master_cue_state);
     map_grv6.tick();
     CHECK(master_cue_calls == before + 1,
           "tick() wrote rbp's master cue again while it was already off");
}

/* Count the K_HPMIX/K_HPLEVEL keycodes in sent[from..], checking each is the
 * hold's value (MIX 0 = CUE, LEVEL 1023 = unity) and that they come in pairs. */
static int hp_holds_since(int from)
{
     int pairs = 0;

     for (int i = from; i < sent_n; i++) {
          if (sent[i].key != K_HPMIX && sent[i].key != K_HPLEVEL)
               continue;
          if (sent[i].key == K_HPLEVEL) {
               CHECK(0, "keycode %d: K_HPLEVEL without a K_HPMIX before it", i);
               continue;
          }
          CHECK(sent[i].kind == 1 && sent[i].op == OP_VALUE &&
                sent[i].ch == CH_GLOBAL && sent[i].param == 0 &&
                sent[i].fval == 0.0f,
                "keycode %d: K_HPMIX param %ld f %.3f, expected 0 (CUE)",
                i, sent[i].param, (double)sent[i].fval);
          CHECK(i + 1 < sent_n && sent[i + 1].key == K_HPLEVEL &&
                sent[i + 1].kind == 1 && sent[i + 1].op == OP_VALUE &&
                sent[i + 1].ch == CH_GLOBAL && sent[i + 1].param == 1023 &&
                sent[i + 1].fval == 1.0f,
                "keycode %d: K_HPMIX not followed by K_HPLEVEL 1023 (unity)", i);
          pairs++;
          i++;
     }
     return pairs;
}

/* tick(): rbp's headphone MIX/LEVEL held, starting with the first tick that
 * finds the engine and then every 100 ticks (2 s at 50 Hz). check_master_cue_hold()
 * ran just before with one engine-less tick and then two with the engine, so
 * the first hold is already in sent[] (counter 0), counter 1 has passed, and
 * the next hold is counter 100: 98 quiet ticks, then the 99th sends. */
static void check_hp_hold(int from)
{
     int start;

     CHECK(hp_holds_since(from) == 1,
           "the first ticks with rbp's mixer sent %d MIX/LEVEL holds, expected 1",
           hp_holds_since(from));

     master_cue_state = 0;            /* the engine exists, master cue off */
     start = sent_n;
     for (int t = 0; t < 98; t++)
          map_grv6.tick();
     CHECK(hp_holds_since(start) == 0,
           "a MIX/LEVEL hold came early (inside the 2 s period)");
     map_grv6.tick();
     CHECK(hp_holds_since(start) == 1,
           "no MIX/LEVEL hold at the 100th tick");
     start = sent_n;
     for (int t = 0; t < 100; t++)
          map_grv6.tick();
     CHECK(hp_holds_since(start) == 1,
           "%d MIX/LEVEL holds in the next 100 ticks, expected 1",
           hp_holds_since(start));
}

/* MASTER_CUE_HW=0: the FLX4's behaviour, rebuilt from a reset table. The two
 * knobs reach rbp again, MASTER CUE toggles rbp's own, and tick() holds
 * nothing. Run last: it replaces the map's bindings. */
static void check_master_cue_hw_off(void)
{
     struct snd_seq_event ev;
     int start, calls;

     ctrl_bindings_reset();
     setenv("MASTER_CUE_HW", "0", 1);
     map_grv6.build();
     unsetenv("MASTER_CUE_HW");

     start = sent_n;
     memset(&ev, 0, sizeof ev);
     ev.type = SNDRV_SEQ_EVENT_CONTROLLER;
     ev.data.control.channel = 6;
     ev.data.control.param = 12;          /* HEADPHONES MIX */
     ev.data.control.value = 127;
     dispatch(&ev);
     ev.data.control.param = 13;          /* HEADPHONES LEVEL */
     ev.data.control.value = 64;
     dispatch(&ev);
     CHECK(sent_n == start + 2 &&
           sent[start].key == K_HPMIX && sent[start].param == 1023 &&
           sent[start + 1].key == K_HPLEVEL && sent[start + 1].param == 516,
           "MASTER_CUE_HW=0: the MIX/LEVEL knobs did not reach rbp (%d keycodes)",
           sent_n - start);

     master_cue_state = 1;
     calls = master_cue_calls;
     start = sent_n;
     for (int t = 0; t < 250; t++)
          map_grv6.tick();
     CHECK(hp_holds_since(start) == 0 && master_cue_calls == calls &&
           master_cue_state == 1,
           "MASTER_CUE_HW=0: tick() still held something (holds %d, master cue "
           "writes %d)", hp_holds_since(start), master_cue_calls - calls);

     memset(&ev, 0, sizeof ev);
     ev.type = SNDRV_SEQ_EVENT_NOTEON;
     ev.data.note.channel = 6;
     ev.data.note.note = 99;              /* MASTER CUE */
     ev.data.note.velocity = 127;
     dispatch(&ev);
     CHECK(master_cue_calls == calls + 1 && master_cue_state == 0,
           "MASTER_CUE_HW=0: MASTER CUE did not toggle rbp's master cue off");
}

static void jog_msg(int ch, int val)
{
     struct snd_seq_event ev;
     memset(&ev, 0, sizeof ev);
     ev.type = SNDRV_SEQ_EVENT_CONTROLLER;
     ev.data.control.channel = (unsigned char)ch;
     ev.data.control.param = 34;          /* the top of the platter */
     ev.data.control.value = val;
     dispatch(&ev);
}

/* The last K_JOG_ROT in sent[] from index `from`, or NULL. */
static const struct sent *last_jog(int from, int *n_nonzero, int *n_total,
                                   int *sign_ok, int want_sign)
{
     const struct sent *last = NULL;
     *n_nonzero = *n_total = 0;
     *sign_ok = 1;
     for (int i = from; i < sent_n; i++) {
          if (sent[i].key != K_JOG_ROT)
               continue;
          (*n_total)++;
          if (sent[i].fval != 0.0f) {
               (*n_nonzero)++;
               if ((sent[i].fval > 0.0f) != (want_sign > 0))
                    *sign_ok = 0;
               if (sent[i].fval > 8.0f || sent[i].fval < -8.0f)
                    *sign_ok = 0;
          }
          last = &sent[i];
     }
     return last;
}

static void check_jog(void)
{
     int from, nz, tot, ok;
     const struct sent *j;

     /* 40 messages of +5 counts, 1 ms apart: 200 counts. The window (8 ms)
      * must collapse them into a handful of speed reports, all forward. */
     sent_n = 0;
     from = 0;
     for (int i = 0; i < 40; i++) {
          jog_msg(0, 0x40 + 5);
          usleep(1000);
     }
     j = last_jog(from, &nz, &tot, &ok, +1);
     CHECK(nz >= 1, "a 40 ms forward turn reported no speed");
     CHECK(nz < 40, "the window did not collapse the messages: %d reports for "
           "40 messages", nz);
     CHECK(ok, "a forward turn reported a backward or out-of-clamp speed");
     CHECK(j == NULL || j->ch == 1, "deck 1's jog went to send channel %d",
           j ? j->ch : -1);

     /* The idle edge: speed 0, carrying the whole position. 200 counts at
      * 6449/rev in 720/rev units is floor(200 * 720 / 6449) = 22. */
     usleep(200000);
     map_grv6.tick();
     j = last_jog(from, &nz, &tot, &ok, +1);
     CHECK(j && j->fval == 0.0f && j->lval == 22,
           "the idle edge sent f=%.3f pos=%ld, expected 0 and 22",
           j ? (double)j->fval : -1.0, j ? j->lval : -1L);
     map_grv6.tick();
     {
          int n2, t2, o2;
          last_jog(from, &n2, &t2, &o2, +1);
          CHECK(t2 == tot, "a second idle tick sent another speed 0");
     }

     /* Back the same 200 counts and one more: the position must step from 0
      * to 0xFFFF (floor division), not sit at 0 for a doubled bucket. */
     from = sent_n;
     for (int i = 0; i < 40; i++) {
          jog_msg(0, 0x40 - 5);
          usleep(1000);
     }
     jog_msg(0, 0x40 - 1);
     j = last_jog(from, &nz, &tot, &ok, -1);
     CHECK(nz >= 1 && ok, "a backward turn reported no speed or a forward one");
     usleep(200000);
     map_grv6.tick();
     j = last_jog(from, &nz, &tot, &ok, -1);
     CHECK(j && j->fval == 0.0f && j->lval == 0xFFFF,
           "one count behind the start read pos=%ld, expected 65535",
           j ? j->lval : -1L);

     /* A nudge shorter than one window still reaches rbp, as the idle edge. */
     from = sent_n;
     jog_msg(1, 0x40 + 2);
     usleep(200000);
     map_grv6.tick();
     j = last_jog(from, &nz, &tot, &ok, +1);
     CHECK(tot == 1 && j && j->ch == 2 && j->fval == 0.0f && j->lval == 0,
           "a 2-count nudge on deck 2 sent %d jog message(s) (ch %d pos %ld); "
           "expected one speed-0 report at pos 0 (2 counts < 1/720 rev)",
           tot, j ? j->ch : -1, j ? j->lval : -1L);

     /* Decks 3/4 never reach the jog. */
     from = sent_n;
     for (int i = 0; i < 20; i++) {
          struct snd_seq_event ev;
          memset(&ev, 0, sizeof ev);
          ev.type = SNDRV_SEQ_EVENT_CONTROLLER;
          ev.data.control.channel = 2;
          ev.data.control.param = 33;
          ev.data.control.value = 0x45;
          dispatch(&ev);
          usleep(1000);
     }
     usleep(200000);
     map_grv6.tick();
     CHECK(sent_n == from, "deck 3's jog produced %d keycodes", sent_n - from);
}

/* ---- the LED table (C-043, measured with C-042's probe 2026-10-13) --------- */

/* The (channel, note) pairs the GRV6 lights BY ITSELF. Sending to any of them
 * from the host would make two writers fight over one LED, so the table must
 * never address one. Each was also sent from the host in the probe and lit
 * nothing, which is the same fact seen from the other side. */
static int self_lit(int ch, int note)
{
     if ((ch == 0 || ch == 1) &&
         (note == 26 || note == 114 || note == 27 || note == 30 ||
          note == 32 || note == 34))
          return 1;                     /* MASTER TEMPO, DECK, pad mode buttons */
     if (ch == 6 && (note == 99 || note == 70 || note == 71))
          return 1;                     /* MASTER CUE, LOAD 1 / 2 */
     if (ch == 4 && note >= 16 && note <= 22)
          return 1;                     /* the FX channel select buttons */
     return 0;
}

static void check_self_lit(const char *what, int ch, int note)
{
     if (ch < 0 || note < 0)
          return;
     CHECK(!self_lit(ch, note),
           "the LED table drives %s at ch %d note %d, which the GRV6 lights by itself",
           what, ch, note);
}

static void check_leds(void)
{
     const struct led_notes *l = map_grv6.leds;

     CHECK(l != NULL, "the GRV6 must declare its measured LED table");
     if (!l)
          return;
     CHECK(l->deck_ch == 0, "deck LEDs start on ch 0, not %d", l->deck_ch);
     CHECK(l->n_play == 11 && l->n_cue == 12 && l->n_sync == 88,
           "PLAY/CUE/SYNC LEDs are 11/12/88 (measured), not %d/%d/%d",
           l->n_play, l->n_cue, l->n_sync);
     CHECK(l->n_loopin == 16 && l->n_loopout == 17,
           "LOOP IN/OUT LEDs are 16/17 (measured), not %d/%d",
           l->n_loopin, l->n_loopout);
     CHECK(l->n_keylock == -1 && l->n_autoloop == -1 && l->n_vinyl == -1,
           "keylock/autoloop/vinyl have no host LED (%d/%d/%d)",
           l->n_keylock, l->n_autoloop, l->n_vinyl);
     CHECK(l->n_slip == 64, "SLIP's LED is its own note 64 (measured), not %d",
           l->n_slip);
     CHECK(l->n_quantize1 == 54, "QUANTIZE's LED is its own note 53 (measured), not %d",
           l->n_quantize1 - 1);
     CHECK(l->jog_ch1 == 16 && l->jog_cc == 0,
           "the jog indicators are ch 15 CC 0.. (measured), not ch %d CC %d",
           l->jog_ch1 - 1, l->jog_cc);
     CHECK(l->cfx_any_ch1 == 7 && l->n_cfx_any == 0,
           "the CFX ON/OFF LED is ch 6 note 0 (measured), not ch1=%d note %d",
           l->cfx_any_ch1, l->n_cfx_any);
     for (int m = 0; m < 4; m++)
          CHECK(l->n_mode[m] == -1,
                "pad mode %d: the GRV6 lights its own mode buttons, not %d",
                m, l->n_mode[m]);
     CHECK(l->pad_ch == 7 && l->pad_ch2 == 9,
           "pad LEDs are on ch 7 / 9 (measured), not %d / %d",
           l->pad_ch, l->pad_ch2);
     CHECK(l->n_pad_base[0] == 0 && l->n_pad_base[1] == 96 &&
           l->n_pad_base[2] == 48 && l->n_pad_base[3] == 32,
           "pad bases by rbp mode are 0/96/48/32 (all seen lit), not %d/%d/%d/%d",
           l->n_pad_base[0], l->n_pad_base[1], l->n_pad_base[2], l->n_pad_base[3]);
     CHECK(l->pad_enc == LED_ENC_NONE,
           "the pads are on/off (blue at 127 only), not encoding %d", l->pad_enc);
     CHECK(l->strip_ch_first == 0 && l->strip_count == 2 && l->n_strip_cue == 84,
           "CH CUE LEDs are 84 on ch 0/1 (measured)");
     CHECK(l->n_master_cue == -1 && l->master_ch_count == 0,
           "MASTER CUE is the GRV6's own light (D21), not note %d x %d",
           l->n_master_cue, l->master_ch_count);
     CHECK(l->fx_ch == 4 && l->n_fx[LED_FX_BFX_ONOFF] == 71,
           "Beat FX ON/OFF is 71 on ch 4 (measured), not %d on %d",
           l->n_fx[LED_FX_BFX_ONOFF], l->fx_ch);
     for (int g = LED_FX_CFX_FILTER; g < LED_FX_COUNT; g++)
          CHECK(l->n_fx[g] == -1, "CFX LED %d: no such button, not %d", g, l->n_fx[g]);
     CHECK(l->meter_ch_first == 0 && l->n_meter_cc == 2,
           "the channel meter is CC 2 on the deck channel (CH1 measured)");
     CHECK(l->meter_master_ch == -1, "no master meter was probed");
     CHECK(l->fx_panel_blinks == 1,
           "the GRV6 blinks FX ON/OFF itself (C-045): fx_panel_blinks must be 1");

     /* Nothing the table can send may land on a self-lit LED. */
     for (int d = 0; d < 2; d++) {
          int ch = l->deck_ch + d;
          check_self_lit("PLAY", ch, l->n_play);
          check_self_lit("CUE", ch, l->n_cue);
          check_self_lit("SYNC", ch, l->n_sync);
          check_self_lit("LOOP IN", ch, l->n_loopin);
          check_self_lit("LOOP OUT", ch, l->n_loopout);
          check_self_lit("KEY LOCK", ch, l->n_keylock);
          check_self_lit("AUTO LOOP", ch, l->n_autoloop);
          for (int m = 0; m < 4; m++)
               check_self_lit("a mode button", ch, l->n_mode[m]);
          for (int m = 0; m < 4; m++)
               for (int p = 0; l->n_pad_base[m] >= 0 && p < 8; p++)
                    check_self_lit("a pad", d ? l->pad_ch2 : l->pad_ch,
                                   l->n_pad_base[m] + p);
     }
     for (int k = 0; k < l->strip_count; k++)
          check_self_lit("a CH CUE", l->strip_ch_first + k, l->n_strip_cue);
     for (int k = 0; k < l->master_ch_count; k++)
          check_self_lit("MASTER CUE", l->master_ch_first + k, l->n_master_cue);
     for (int g = 0; g < LED_FX_COUNT; g++)
          check_self_lit("an FX LED", l->fx_ch, l->n_fx[g]);
}

/* The 2026-10-23 additions, driven event by event: the rows the inventory
 * bound, and map_ux.c's behaviours as the GRV6 reaches them. */
static void note_ev(int ch, int note, int on)
{
     struct snd_seq_event ev;
     memset(&ev, 0, sizeof ev);
     ev.type = on ? SNDRV_SEQ_EVENT_NOTEON : SNDRV_SEQ_EVENT_NOTEOFF;
     ev.data.note.channel = ch;
     ev.data.note.note = note;
     ev.data.note.velocity = on ? 127 : 0;
     dispatch(&ev);
}

static void cc_ev(int ch, int cc, int val)
{
     struct snd_seq_event ev;
     memset(&ev, 0, sizeof ev);
     ev.type = SNDRV_SEQ_EVENT_CONTROLLER;
     ev.data.control.channel = ch;
     ev.data.control.param = cc;
     ev.data.control.value = val;
     dispatch(&ev);
}

static void press(int ch, int note) { note_ev(ch, note, 1); note_ev(ch, note, 0); }

/* sent[from..] is exactly `n` of `want` (kind 0/2 key+op+ch+param). */
static void expect_keys(const char *what, int from, const struct sent *want, int n)
{
     CHECK(sent_n - from == n, "%s: %d keycodes, expected %d", what, sent_n - from, n);
     for (int i = 0; i < n && from + i < sent_n; i++) {
          const struct sent *g = &sent[from + i];
          CHECK(g->key == want[i].key && g->op == want[i].op &&
                g->ch == want[i].ch && g->param == want[i].param,
                "%s #%d: got 0x%04x op%d ch%d p%ld, expected 0x%04x op%d ch%d p%ld",
                what, i, g->key, g->op, g->ch, g->param,
                want[i].key, want[i].op, want[i].ch, want[i].param);
     }
}

#define K(key, op, ch, p) { 0, (key), (op), (ch), (p), 0.0f, 0 }

static void check_inventory_rows(void)
{
     int s0;

     s0 = sent_n; press(0, 64);
     { const struct sent w[] = { K(K_SLIP, OP_PRESS, 1, 0), K(K_SLIP, OP_RELEASE, 1, 0) };
       expect_keys("SLIP", s0, w, 2); }
     s0 = sent_n; press(1, 23);
     { const struct sent w[] = { K(K_VINYL, OP_PRESS, 2, 0), K(K_VINYL, OP_RELEASE, 2, 0) };
       expect_keys("SHIFT + SLIP (deck 2)", s0, w, 2); }
     s0 = sent_n; press(0, 53);
     { const struct sent w[] = { K(K_QUANTIZE, OP_PRESS, 1, 0), K(K_QUANTIZE, OP_RELEASE, 1, 0) };
       expect_keys("QUANTIZE", s0, w, 2); }
     s0 = sent_n; press(0, 96);
     { const struct sent w[] = { K(K_TEMPO_RANGE, OP_PRESS, 1, 0), K(K_TEMPO_RANGE, OP_RELEASE, 1, 0) };
       expect_keys("SHIFT + MASTER TEMPO", s0, w, 2); }
     s0 = sent_n; press(0, 92);
     { const struct sent w[] = { K(K_MASTER, OP_PRESS, 1, 0), K(K_MASTER, OP_RELEASE, 1, 0) };
       expect_keys("SHIFT + BEAT SYNC", s0, w, 2); }
     s0 = sent_n; press(6, 103);
     { const struct sent w[] = { K(K_TAGLIST, OP_PRESS, CH_GLOBAL, 0), K(K_TAGLIST, OP_RELEASE, CH_GLOBAL, 0) };
       expect_keys("SHIFT + VIEW", s0, w, 2); }
     s0 = sent_n; press(4, 107);
     { const struct sent w[] = { K(K_TAP, OP_PRESS, CH_GLOBAL, 0), K(K_TAP, OP_RELEASE, CH_GLOBAL, 0) };
       expect_keys("SHIFT + BEAT >", s0, w, 2); }
     /* Nothing at all: DRUM SWAP, CAPTURE, SHIFT + QUANTIZE, SHIFT + MEMORY. */
     s0 = sent_n;
     press(0, 0); press(0, 3); press(0, 8); press(0, 104); press(0, 62);
     CHECK(sent_n == s0, "an unbound Groove/SHIFT control sent %d keycodes", sent_n - s0);

     /* SHIFT + tilt DOWN: a page of selector steps; SHIFT + LEFT is BACK. */
     s0 = sent_n; press(6, 59);
     CHECK(sent_n - s0 == 10, "SHIFT + tilt DOWN sent %d keycodes, expected 10", sent_n - s0);
     for (int i = s0; i < sent_n; i++)
          CHECK(sent[i].key == K_SELECTOR && sent[i].op == OP_ROTATE && sent[i].param == 1,
                "SHIFT + tilt DOWN step %d is not a selector +1", i - s0);
     s0 = sent_n; press(6, 61);
     { const struct sent w[] = { K(K_BACK, OP_PRESS, CH_GLOBAL, 0), K(K_BACK, OP_RELEASE, CH_GLOBAL, 0) };
       expect_keys("SHIFT + tilt LEFT", s0, w, 2); }
}

static void check_ux(void)
{
     int s0;

     /* CFX ON/OFF: both channels OFF -> Filter on each (rbp's type is per
      * channel); SHIFT steps both to Space; ON/OFF again switches both off. */
     sim_cfx[0] = sim_cfx[1] = 0;
     s0 = sent_n; press(6, 0);
     { const struct sent w[] = { K(K_FILTER, OP_PRESS, 1, 0), K(K_FILTER, OP_RELEASE, 1, 0),
                                 K(K_FILTER, OP_PRESS, 2, 0), K(K_FILTER, OP_RELEASE, 2, 0) };
       expect_keys("SOUND COLOR FX ON/OFF", s0, w, 4); }
     s0 = sent_n; press(6, 8);
     { const struct sent w[] = { K(K_SPACE, OP_PRESS, 1, 0), K(K_SPACE, OP_RELEASE, 1, 0),
                                 K(K_SPACE, OP_PRESS, 2, 0), K(K_SPACE, OP_RELEASE, 2, 0) };
       expect_keys("SHIFT + SOUND COLOR FX ON/OFF", s0, w, 4); }
     /* One channel switched off on rbp's own screen: ON/OFF reads "on" (the
      * other still is) and switches off only the channel that is on. */
     sim_cfx[1] = 0;
     s0 = sent_n; press(6, 0);
     { const struct sent w[] = { K(K_SPACE, OP_PRESS, 1, 0), K(K_SPACE, OP_RELEASE, 1, 0) };
       expect_keys("SOUND COLOR FX ON/OFF with one channel on", s0, w, 2); }
     CHECK(sim_cfx[0] == 0 && sim_cfx[1] == 0, "CFX off left %d/%d", sim_cfx[0], sim_cfx[1]);

     /* The browse knob on the performance screen opens the browser; SHIFT turns
      * it are the selector; SHIFT + push there does nothing. */
     browse_mode = 1;
     s0 = sent_n; cc_ev(6, 64, 1);
     { const struct sent w[] = { K(K_BROWSE, OP_PRESS, CH_GLOBAL, 0), K(K_BROWSE, OP_RELEASE, CH_GLOBAL, 0) };
       expect_keys("browse turn on the performance screen", s0, w, 2); }
     s0 = sent_n; cc_ev(6, 100, 127);
     { const struct sent w[] = { K(K_SELECTOR, OP_ROTATE, CH_GLOBAL, -1) };
       expect_keys("SHIFT + browse turn on the performance screen", s0, w, 1); }
     s0 = sent_n; press(6, 66);
     CHECK(sent_n == s0, "SHIFT + push on the performance screen sent %d keycodes", sent_n - s0);
     browse_mode = 3;

     /* SHIFT + platter: SEARCH held in the turn's direction, released when idle. */
     s0 = sent_n; cc_ev(0, 41, 0x41); cc_ev(0, 41, 0x42);
     { const struct sent w[] = { K(K_SRFWD, OP_PRESS, 1, 0) };
       expect_keys("SHIFT + platter forward", s0, w, 1); }
     usleep((UX_SEARCH_IDLE_MS + 30) * 1000);
     s0 = sent_n; map_grv6.tick();
     int found = 0;
     for (int i = s0; i < sent_n; i++)
          if (sent[i].key == K_SRFWD && sent[i].op == OP_RELEASE)
               found = 1;
     CHECK(found, "SEARCH > was not released after the platter stopped");

     /* DRUM ROLL 2 (1/4 beat) from HOT CUE: SLIP mode, pad 3 held, HOT CUE back. */
     pad_mode[0] = 0;
     s0 = sent_n; note_ev(0, 5, 1);
     { const struct sent w[] = { K(K_SLIPLOOP, OP_PRESS, 1, 0), K(K_SLIPLOOP, OP_RELEASE, 1, 0),
                                 K(K_PAD1 + 2, OP_PRESS, 1, 0) };
       expect_keys("DRUM ROLL 2 press", s0, w, 3); }
     s0 = sent_n; note_ev(0, 5, 0);
     { const struct sent w[] = { K(K_PAD1 + 2, OP_RELEASE, 1, 0),
                                 K(K_HOTCUE, OP_PRESS, 1, 0), K(K_HOTCUE, OP_RELEASE, 1, 0) };
       expect_keys("DRUM ROLL 2 release", s0, w, 3); }
     /* Already in SLIP: no mode key either way (it would open the size bank). */
     pad_mode[1] = 2;
     s0 = sent_n; press(1, 4);
     { const struct sent w[] = { K(K_PAD1 + 1, OP_PRESS, 2, 0), K(K_PAD1 + 1, OP_RELEASE, 2, 0) };
       expect_keys("DRUM ROLL 1 in SLIP mode", s0, w, 2); }

     /* The lever: VINYL BRAKE on deck 1 while tilted, then the knob's last
      * position (DELAY, the fixture's last click) and last FX target (MST) put
      * back -- the map's record, not rbp's getter (map_ux.c). */
     bfx_led = 1;
     s0 = sent_n; note_ev(0, 36, 1);
     { const struct sent w[] = { K(K_BFXTYPE, OP_VALUE, CH_GLOBAL, 13), K(K_BFXCH, OP_VALUE, CH_GLOBAL, 0),
                                 K(K_BFX, OP_PRESS, CH_GLOBAL, 0), K(K_BFX, OP_RELEASE, CH_GLOBAL, 0) };
       expect_keys("DRUM RELEASE tilted", s0, w, 4); }
     s0 = sent_n; note_ev(0, 36, 0);
     { const struct sent w[] = { K(K_BFX, OP_PRESS, CH_GLOBAL, 0), K(K_BFX, OP_RELEASE, CH_GLOBAL, 0),
                                 K(K_BFXTYPE, OP_VALUE, CH_GLOBAL, 0),   /* the knob's last: DELAY */
                                 K(K_BFXCH, OP_VALUE, CH_GLOBAL, BFX_CH_MASTER) };
       expect_keys("DRUM RELEASE back", s0, w, 4); }
     /* Beat FX already on: the lever leaves it alone. */
     bfx_led = 2;
     s0 = sent_n; press(0, 36);
     CHECK(sent_n == s0, "the lever took over a running Beat FX (%d keycodes)", sent_n - s0);
     bfx_led = 1;
}

/* SHIFT + BACK held RETURN_HOLD_MS writes the launcher's request; a short one
 * does not. */
static void check_return(void)
{
     char path[] = "/tmp/test_grv6_return.XXXXXX";
     int fd = mkstemp(path);
     char buf[16] = { 0 };

     CHECK(fd >= 0, "mkstemp failed");
     if (fd < 0)
          return;
     close(fd);
     unlink(path);
     setenv("RETURN_HOLD_MS", "100", 1);
     setenv("RETURN_REQUEST", path, 1);
     ux_config();

     note_ev(6, 102, 1);
     usleep(30 * 1000);
     map_grv6.tick();
     note_ev(6, 102, 0);
     CHECK(access(path, F_OK) != 0, "a short SHIFT + BACK wrote the return request");

     note_ev(6, 102, 1);
     usleep(130 * 1000);
     map_grv6.tick();
     note_ev(6, 102, 0);
     fd = open(path, O_RDONLY);
     CHECK(fd >= 0, "SHIFT + BACK held did not write %s", path);
     if (fd >= 0) {
          (void)!read(fd, buf, sizeof buf - 1);
          close(fd);
          CHECK(strcmp(buf, "grv6\n") == 0, "the request reads '%s', expected 'grv6'", buf);
     }
     unlink(path);
     unsetenv("RETURN_HOLD_MS");
     unsetenv("RETURN_REQUEST");
     ux_config();
}

int main(void)
{
     static const char *const names[] = {
          "KNOB_SCALE", "JOG_SCALE", "JOG_PPR", "JOG_REV", "JOG_IDLE_MS",
          "JOG_WINDOW_MS", "JOG_VERBOSE", "TEMPO_VERBOSE", "TEMPO_REV",
          "KNOB_VERBOSE", "MIRROR_GAIN_MID", "MASTER_CUE_HW",
     };
     int n;

     for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
          unsetenv(names[i]);

     round_trip();
     map_grv6.build();

     /* Decks: 22 notes each (6 transport + MASTER TEMPO + 8 pad modes + 7
      * CALL/MEMORY/log-only) + 13 from the 2026-10-23 inventory (SLIP, VINYL,
      * QUANTIZE, TEMPO RANGE, MASTER, 4 log-only SHIFT layers, DRUM SWAP x4,
      * CAPTURE) and 5 strip controls; pads: 4 bound bases x 8 per deck; mixer:
      * 14 notes (12, less SHIFT + push now ahead of the table, plus SHIFT + tilt
      * LEFT/RIGHT and SHIFT + VIEW) and 5 knobs; Beat FX: 2 notes and 1 knob. */
     CHECK(note_map_n == 150,
           "the map built %d note rows, expected 150 (2 x (35 + 32) + 14 + 2)",
           note_map_n);
     CHECK(abs_map_n == 16,
           "the map built %d absolute rows, expected 16 (2 x 5 + 5 + 1)",
           abs_map_n);
     check_leds();
     CHECK(strcmp(map_grv6.name, "grv6") == 0, "the map is named '%s'",
           map_grv6.name);

     n = mididump_replay(GRV6_FIXTURE, 0.0, dispatch);
     CHECK(n == fixture_events, "the replay dispatched %d events, expected %d",
           n, fixture_events);

     check_stream();
     check_state();
     {
          int from = sent_n;
          check_master_cue_hold();
          check_hp_hold(from);
     }
     check_jog();
     check_inventory_rows();
     check_ux();
     check_return();
     check_master_cue_hw_off();

     printf("test_grv6: %d checks, %d failures\n", checks, failures);
     return failures == 0 ? 0 : 1;
}
