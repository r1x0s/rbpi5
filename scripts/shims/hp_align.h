/*
 * hp_align.h -- the headphone pair's timing against the master pair.
 *
 * WHY THIS EXISTS. On the FLX4 the headphone jack plays rbp's phone stream and
 * nothing else, so how that stream lines up with the master stream never
 * mattered: the two never meet. On the DDJ-GRV6 they do. The GRV6 mixes the
 * headphones IN HARDWARE -- its MASTER CUE and its HEADPHONES MIX knob blend
 * USB channels 0/1 (master) into channels 2/3 (the cue bus) inside the
 * controller (MASTER CUE: measured on the unit, 2026-10-13; the MIX knob:
 * Algoriddim's support, "mixes audio signals inside the hardware"). So with a
 * channel CUE on and that channel's fader up, the same track reaches the ear
 * twice -- pre-fader on 2/3, post-fader on 0/1 -- and any offset between rbp's
 * phone and master streams is a comb filter. docs/09 records the same lesson
 * from the SC Live 4: rbp's separate streams are "not sample-aligned, and
 * summing them comb-filtered". The owner's report (2026-10-13 20:55): "the cues
 * cancel each other out".
 *
 * This file is the two halves of the answer, both pure (no ALSA, no clock, no
 * allocation, no float in the per-block path -- the shim is built soft-float,
 * where a float multiply is a library call):
 *
 *   hp_delay   a fixed delay (and optional polarity flip) on ONE pair of an
 *              interleaved frame -- RB_AUDIO_HP_DELAY / RB_AUDIO_HP_INVERT;
 *   hp_probe   a cross-correlation of the headphone pair against the master
 *              pair as they go to the card, spread over many blocks, that says
 *              by how many frames (and with which sign) they are apart --
 *              RB_AUDIO_ALIGN_PROBE=1. It measures; it never changes the audio.
 *
 * The probe reads the frame AFTER the delay, so its answer is the residual: 0
 * means aligned, and `delay + lag' is the value to set.
 *
 * SIGN CONVENTION, pinned by test_align: lag > 0 means the master pair carries a
 * sound `lag' frames AFTER the headphone pair does (the headphones are early),
 * which is what a headphone delay of `lag' frames cancels. lag < 0 means the
 * headphones are late, which a headphone delay cannot fix.
 */
#ifndef RBPI4B_HP_ALIGN_H
#define RBPI4B_HP_ALIGN_H

#include <stdint.h>

/* ---- the delay ------------------------------------------------------------ */

/* 4096 frames is 93 ms at 44.1 kHz: far more than any processing latency inside
 * one engine, and still only 32 KB of ring. */
#define HP_DELAY_MAX 4096

struct hp_delay {
    int32_t ring[HP_DELAY_MAX * 2];
    unsigned d;          /* frames of delay, 0..HP_DELAY_MAX */
    unsigned pos;        /* next ring slot, 0..d-1 */
};

/* Set the delay to `frames' (clamped to HP_DELAY_MAX) and clear the ring, so the
 * first `frames' frames out are silence rather than stale audio. Returns the
 * delay actually set. */
unsigned hp_delay_init(struct hp_delay *dl, long frames);

/* Run `frames' frames of channels a and b of an interleaved buffer (`stride'
 * channels per frame) through the delay, in place, and negate them if `invert'.
 * Every other channel is untouched. With d == 0 and !invert it is a no-op. */
void hp_delay_run(struct hp_delay *dl, int32_t *buf, unsigned frames,
                  unsigned stride, unsigned a, unsigned b, int invert);

/* ---- the probe ------------------------------------------------------------ */

/* Lags searched: +-1024 frames (+-23 ms). Window: 4096 frames (93 ms). */
#define HP_PROBE_MAX_LAG 1024
#define HP_PROBE_WIN     4096
#define HP_PROBE_HIST    (HP_PROBE_WIN + 2 * HP_PROBE_MAX_LAG)

/* Below this mean square (on the 16-bit mono mix) a window is not evidence:
 * 64 is an RMS of 8/32768, about -72 dBFS -- silence, a muted pair or dither. */
#define HP_PROBE_MIN_MS 64

struct hp_probe_result {
    int valid;           /* 0: one of the pairs was (near) silent */
    int lag;             /* the best lag, frames, sign as in the header */
    int corr_milli;      /* normalised correlation at that lag, -1000..1000 */
    int corr0_milli;     /* the same at lag 0, for comparison */
};

struct hp_probe {
    int16_t m[HP_PROBE_HIST];        /* master, mono, 16-bit */
    int16_t p[HP_PROBE_HIST];        /* headphones, mono, 16-bit */
    int64_t msq[HP_PROBE_HIST + 1];  /* prefix sums of m[]^2 */
    unsigned fill;                   /* frames collected, 0..HP_PROBE_HIST */
    int computing;                   /* 1 once full: feed() ignores input */
    int next_lag;                    /* the next lag step() computes */
    int64_t pe;                      /* sum of p^2 over the window */
    double best_score;               /* signed C*|C|/E(lag) of the best so far */
    int best_lag;
    int64_t c0;                      /* the correlation sum at lag 0 */
};

void hp_probe_reset(struct hp_probe *pr);

/* Append frames to the probe from an interleaved buffer: master = channels
 * ma/mb, headphones = pa/pb. Samples are the shim's sign-extended 24-bit values.
 * Ignored while a window is being computed. */
void hp_probe_feed(struct hp_probe *pr, const int32_t *buf, unsigned frames,
                   unsigned stride, unsigned ma, unsigned mb,
                   unsigned pa, unsigned pb);

/* Compute up to `nlags' more lags of the current window. Returns 1 and fills
 * *out when the window is finished (the probe then collects the next one),
 * 0 otherwise. The cost is nlags * HP_PROBE_WIN integer multiply-adds. */
int hp_probe_step(struct hp_probe *pr, int nlags, struct hp_probe_result *out);

#endif
