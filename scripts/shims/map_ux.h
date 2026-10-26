/*
 * map_ux.h — the operator-facing behaviours the DDJ maps share.
 *
 * map_flx4.c grew these one by one at the operator's request (2026-10-22): the
 * browse knob opening the browser, SHIFT + browse push leaving it, the CFX
 * button, SHIFT + platter search and the held "back to the boot menu" combo.
 * The GRV6 gets the same ones (2026-10-23), so they live here once rather than
 * as two copies that drift. Each map still decides WHICH control does what --
 * the notes are the map's -- and calls in here for the behaviour.
 *
 * Everything here talks to rbp only through rbp_bridge.h / rbp_key.h, so the
 * map tests stub it exactly as they stub the maps' own calls.
 */
#ifndef MAP_UX_H
#define MAP_UX_H

/* Reads RETURN_HOLD_MS (default 2000) and RETURN_REQUEST (default
 * /tmp/rblaunch.request). Call from the map's build(). */
void ux_config(void);

/* ---- back to the boot menu ------------------------------------------------
 * The combo is HELD for RETURN_HOLD_MS so a brush of it mid-set cannot end the
 * set; a release before that cancels. Writing RETURN_REQUEST is the whole
 * request: the launcher's rblaunch-return.path watches it on the host, and the
 * chroot's /tmp is the host's (fix-dev.sh). `who` names the surface in the file
 * and the log. */
void ux_return_edge(int on);
void ux_return_tick(const char *who);

/* ---- SHIFT + platter = SEARCH ---------------------------------------------
 * rbp's SEARCH < / > are held keys, so the platter is a hold in the direction
 * it turns: the first count presses, a count the other way swaps the press, and
 * UX_SEARCH_IDLE_MS without a count releases (ux_search_idle(), from tick()).
 * deck is 0 or 1; dir is the platter's sign after any JOG_REV flip. */
#define UX_SEARCH_IDLE_MS 150
void ux_search_turn(int deck, int dir);
void ux_search_idle(void);

/* ---- the CFX button ---------------------------------------------------------
 * rbp's Sound Color FX type is ONE selection for the mixer, and pressing the
 * type that is on switches it off. So the plain press toggles CFX (it presses
 * the current type) and the SHIFT press steps Filter -> Space -> Dub Echo ->
 * Sweep -> Noise -> Crush, which also switches it on. ux_cfx_start() is the
 * map's startup: ONE Filter press (a second press -- e.g. one per channel --
 * would be "the same type again" and switch it straight back off). */
void ux_cfx_start(void);
void ux_cfx_button(int shift);

/* ---- the browse knob ----------------------------------------------------------
 * On the performance screen rbp's selector zooms the waveform or moves the grid,
 * which on a knob labelled BROWSE reads as the knob doing the wrong thing. So
 * there the PLAIN turn opens the browser and the SHIFT turn is rbp's selector;
 * everywhere else both are the selector. Detents inside UX_BROWSE_SETTLE_MS of
 * the BROWSE press are dropped: the screen changes a frame or more later, and
 * they would still read PLAY. delta is the knob's signed step. */
#define UX_BROWSE_SETTLE_MS 400
void ux_browse_turn(int shifted, int delta);

/* SHIFT + browse push: out of the browser (rbp's BROWSE key toggles the screen
 * it opens); nothing on the performance screen, where the plain turn opens it. */
void ux_browse_exit(void);

/* ---- a momentary SLIP BEAT LOOP -------------------------------------------------
 * Held: the deck switches to rbp's SLIP BEAT LOOP pad mode (unless it is in it)
 * and that mode's pad `pad` (1..8) is held -- a slip roll, the playhead carrying
 * on underneath. Released: the pad is let go and the deck's previous pad mode is
 * restored. rbp's SLIP pads are 1/16, 1/8, 1/4, 1/2, 1, 2, 1/3, 3/4 beats
 * (docs/08-controls.md). The GRV6's DRUM ROLL 1-4 use pads 2-5. */
void ux_slip_roll(int deck, int pad, int on);

/* ---- a momentary Beat FX ---------------------------------------------------------
 * Held: Beat FX switches to effect position `pos` on deck `deck` and on;
 * released: off again, and the type and target the operator had are put back.
 * Does nothing (logged) while Beat FX is already on: it is in use, and taking it
 * over would end what the operator is doing. `fx_ch` is the target the map last
 * sent (-1 = unknown: then the target stays on the deck afterwards), `type_pos`
 * the effect position the map last sent (-1 = unknown: then rbp's own type is
 * read, and if that has no position the brake stays selected). The GRV6's
 * DRUM RELEASE lever uses it with VINYL BRAKE. */
#define UX_BFX_POS_VINYL_BRAKE 13
void ux_bfx_momentary(int deck, int pos, int fx_ch, int type_pos, int on);

#endif
