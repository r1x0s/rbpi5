/*
 * map_ux.c — the operator-facing behaviours the DDJ maps share. See map_ux.h.
 *
 * Moved here from map_flx4.c on 2026-10-23, unchanged in behaviour (test_flx4
 * pins it), when the GRV6 map took the same set.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "shimutil.h"
#include "rbp_abi.h"
#include "rbp_bridge.h"
#include "rbp_key.h"
#include "rbp_led.h"
#include "map_ux.h"

/* ---- back to the boot menu ---------------------------------------------- */

static int return_hold_ms = 2000;
static const char *return_request = "/tmp/rblaunch.request";
static unsigned long long return_down_ms;     /* 0 = not held */
static int return_sent;

void ux_config(void)
{
     return_hold_ms = env_num("RETURN_HOLD_MS", 2000);
     return_request = env_text("RETURN_REQUEST", "/tmp/rblaunch.request");
}

void ux_return_edge(int on)
{
     if (on) {
          return_down_ms = shim_now_ms();
          return_sent = 0;
     } else {
          return_down_ms = 0;
     }
}

void ux_return_tick(const char *who)
{
     if (!return_down_ms || return_sent)
          return;
     if (shim_now_ms() - return_down_ms < (unsigned long long)return_hold_ms)
          return;
     return_sent = 1;
     int fd = open(return_request, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
     if (fd >= 0) {
          (void)!write(fd, who, strlen(who));
          (void)!write(fd, "\n", 1);
          close(fd);
     }
     klog("knobshim2: %s: return combo held -> back to the boot menu (%s: %s)\n",
          who, return_request, fd >= 0 ? "written" : "could not write");
}

/* ---- SHIFT + platter = SEARCH ---------------------------------------------- */

static struct {
     int dir;                       /* -1, 0 (nothing held) or +1 */
     unsigned long long last_ms;
} search_state[2];

static void search_release(int idx)
{
     if (search_state[idx].dir == 0)
          return;
     send_rx_key(search_state[idx].dir > 0 ? K_SRFWD : K_SRREV, OP_RELEASE, 1 + idx, 0);
     search_state[idx].dir = 0;
}

void ux_search_turn(int deck, int dir)
{
     int idx = deck ? 1 : 0;
     search_state[idx].last_ms = shim_now_ms();
     if (search_state[idx].dir == dir)
          return;
     search_release(idx);
     send_rx_key(dir > 0 ? K_SRFWD : K_SRREV, OP_PRESS, 1 + idx, 0);
     search_state[idx].dir = dir;
     if (verbose)
          klog("knobshim2: SHIFT+jog deck %d -> SEARCH %s\n", idx + 1, dir > 0 ? ">" : "<");
}

void ux_search_idle(void)
{
     unsigned long long t = shim_now_ms();
     for (int i = 0; i < 2; i++)
          if (search_state[i].dir != 0 &&
              t - search_state[i].last_ms >= UX_SEARCH_IDLE_MS)
               search_release(i);
}

/* ---- the CFX button ----------------------------------------------------------
 *
 * rbp's Sound Color FX type is PER MIXER CHANNEL (rbp_abi.h, ME_GET_CFX_TYPE):
 * a type key acts on the channel it is sent to -- it selects that type there,
 * or switches it off if it is the one already selected. Until 2026-10-23 this
 * pressed one key on channel 1 only, so channel 2 never had a type and its CFX
 * knob did nothing (the operator's GRV6 report). So every gesture now reads
 * both channels' types and presses, per channel, only what moves that channel
 * to the state wanted. */

/* rbp's type numbers (0 OFF .. 6 CRUSH) -> their keys, and the cycle order. */
static const int cfx_type_key[7] = { 0, K_FILTER, K_NOISE, K_SWEEP, K_DUBECHO, K_SPACE, K_CRUSH };
static const int cfx_cycle[] = { 1, 5, 4, 3, 2, 6 };   /* Filter Space DubEcho Sweep Noise Crush */
#define CFX_CYCLE_N ((int)(sizeof cfx_cycle / sizeof cfx_cycle[0]))
static int cfx_pos;            /* index into cfx_cycle: the type the button selects */
static int cfx_off;            /* the blind fallback's own record (no engine read) */

static void cfx_press(int key, int sch)
{
     send_rx_key(key, OP_PRESS, sch, 0);
     send_rx_key(key, OP_RELEASE, sch, 0);
}

/* Bring both channels to `want` (a type number; 0 = off). Returns 0 when the
 * engine could not be read, so the caller falls back. */
static int cfx_apply(int want)
{
     int t[2] = { me_get_cfx_type(0), me_get_cfx_type(1) };

     if (t[0] < 0 || t[1] < 0)
          return 0;
     for (int ch = 0; ch < 2; ch++) {
          if (t[ch] == want)
               continue;
          if (want == 0) {
               if (t[ch] >= 1 && t[ch] <= 6)
                    cfx_press(cfx_type_key[t[ch]], ch + 1);   /* same type again = off */
          } else {
               cfx_press(cfx_type_key[want], ch + 1);
          }
     }
     return 1;
}

void ux_cfx_start(void)
{
     cfx_pos = 0;
     cfx_off = 0;
     if (!cfx_apply(cfx_cycle[0])) {
          cfx_press(K_FILTER, 1);
          cfx_press(K_FILTER, 2);
     }
}

void ux_cfx_button(int shift)
{
     int want;

     if (shift) {
          cfx_pos = (cfx_pos + 1) % CFX_CYCLE_N;
          want = cfx_cycle[cfx_pos];
     } else {
          int t0 = me_get_cfx_type(0), t1 = me_get_cfx_type(1);
          int any_on = (t0 > 0) || (t1 > 0);
          if (t0 < 0 || t1 < 0)
               any_on = !cfx_off;
          want = any_on ? 0 : cfx_cycle[cfx_pos];
     }
     if (!cfx_apply(want)) {
          int key = cfx_type_key[cfx_cycle[cfx_pos]];
          cfx_press(key, 1);
          cfx_press(key, 2);
     }
     cfx_off = (want == 0);
     klog("knobshim2: CFX button -> %s (type %d)\n", want ? "on" : "off",
          cfx_cycle[cfx_pos]);
}

/* ---- the browse knob ----------------------------------------------------------- */

static unsigned long long browse_open_ms;

static void browse_key(void)
{
     send_rx_key(K_BROWSE, OP_PRESS, CH_GLOBAL, 0);
     send_rx_key(K_BROWSE, OP_RELEASE, CH_GLOBAL, 0);
}

void ux_browse_turn(int shifted, int delta)
{
     if (delta == 0)
          return;
     if (!shifted && rbp_browse_mode() == BROWSE_MODE_PLAY) {
          unsigned long long t = shim_now_ms();
          if (browse_open_ms != 0 && t - browse_open_ms < UX_BROWSE_SETTLE_MS)
               return;
          browse_open_ms = t;
          browse_key();
          if (verbose)
               klog("knobshim2: browse knob on the performance screen -> BROWSE\n");
          return;
     }
     send_rx_key(K_SELECTOR, OP_ROTATE, CH_GLOBAL, delta < 0 ? -1 : 1);
     if (verbose)
          klog("knobshim2: browse knob delta=%d -> 0x%04x rotate\n", delta, K_SELECTOR);
}

void ux_browse_exit(void)
{
     if (rbp_browse_mode() == BROWSE_MODE_PLAY)
          return;
     browse_key();
     if (verbose)
          klog("knobshim2: SHIFT + browse push -> BROWSE (leave the browser)\n");
}

/* ---- a momentary SLIP BEAT LOOP ------------------------------------------------- */

/* rbp's pad-mode keys by mode number (0 HOT CUE, 1 AUTO BEAT LOOP, 2 SLIP BEAT
 * LOOP, 3 BEAT JUMP -- rbp_abi.h UI_GET_PADMODE). */
static const int padmode_key[4] = { K_HOTCUE, K_ALOOP, K_SLIPLOOP, K_BEATJUMP };
#define PADMODE_SLIP 2

static struct {
     int pad;          /* the pad held, 1..8; 0 = none */
     int prev_mode;    /* the mode to put back; -1 = leave it */
} roll_state[2];

static void key_tap(int key, int sch)
{
     send_rx_key(key, OP_PRESS, sch, 0);
     send_rx_key(key, OP_RELEASE, sch, 0);
}

void ux_slip_roll(int deck, int pad, int on)
{
     int idx = deck ? 1 : 0, sch = idx + 1;

     if (pad < 1 || pad > 8)
          return;
     if (on) {
          if (roll_state[idx].pad)              /* one roll per deck at a time */
               return;
          int mode = rbp_padmode(idx);
          /* Pressing the mode rbp is already in opens its size bank (rbp_led.c,
           * mode 4), so the key goes out only for a real change. */
          if (mode != PADMODE_SLIP)
               key_tap(K_SLIPLOOP, sch);
          roll_state[idx].prev_mode = (mode >= 0 && mode <= 3 && mode != PADMODE_SLIP)
                                      ? mode : -1;
          roll_state[idx].pad = pad;
          send_rx_key(K_PAD1 + pad - 1, OP_PRESS, sch, 0);
          if (verbose)
               klog("knobshim2: deck %d slip roll pad %d (mode %d -> SLIP)\n",
                    sch, pad, mode);
     } else {
          if (roll_state[idx].pad != pad)
               return;
          send_rx_key(K_PAD1 + pad - 1, OP_RELEASE, sch, 0);
          if (roll_state[idx].prev_mode >= 0)
               key_tap(padmode_key[roll_state[idx].prev_mode], sch);
          if (verbose)
               klog("knobshim2: deck %d slip roll pad %d released (mode back to %d)\n",
                    sch, pad, roll_state[idx].prev_mode);
          roll_state[idx].pad = 0;
     }
}

/* ---- a momentary Beat FX ---------------------------------------------------------- */

#define LEDSTAT_BFX_ONOFF 48   /* rbp_led.c's led_g_tab: 2 = the effect is on */

static struct {
     int active;       /* this module switched Beat FX on */
     int prev_pos;     /* the type position to put back; -1 = leave it */
     int prev_ch;      /* the target to put back; -1 = leave it */
} bfx_mom;

void ux_bfx_momentary(int deck, int pos, int fx_ch, int type_pos, int on)
{
     if (on) {
          if (bfx_mom.active)
               return;
          if (rbp_ledstat_state(LEDSTAT_BFX_ONOFF, 0) == 2) {
               klog("knobshim2: momentary Beat FX: Beat FX is already on, left alone\n");
               return;
          }
          /* The map's own record first: on the unit (2026-10-23) rbp's getter
           * answered a type with no position at this point every time, and the
           * brake stayed selected after the lever came back. */
          if (type_pos >= 0) {
               bfx_mom.prev_pos = type_pos;
          } else {
               int t = rbp_beatfx_type();
               bfx_mom.prev_pos = (t >= 0 && t <= 14) ? bfx_type_to_pos[t] : -1;
          }
          bfx_mom.prev_ch = fx_ch;
          bfx_mom.active = 1;
          send_rx_key(K_BFXTYPE, OP_VALUE, CH_GLOBAL, pos);
          send_rx_key(K_BFXCH, OP_VALUE, CH_GLOBAL, deck ? 1 : 0);
          key_tap(K_BFX, CH_GLOBAL);
          klog("knobshim2: momentary Beat FX on: position %d, deck %d\n", pos, deck + 1);
     } else {
          if (!bfx_mom.active)
               return;
          bfx_mom.active = 0;
          key_tap(K_BFX, CH_GLOBAL);
          if (bfx_mom.prev_pos >= 0)
               send_rx_key(K_BFXTYPE, OP_VALUE, CH_GLOBAL, bfx_mom.prev_pos);
          if (bfx_mom.prev_ch >= 0)
               send_rx_key(K_BFXCH, OP_VALUE, CH_GLOBAL, bfx_mom.prev_ch);
          klog("knobshim2: momentary Beat FX off (type %d, target %d restored)\n",
               bfx_mom.prev_pos, bfx_mom.prev_ch);
     }
}
