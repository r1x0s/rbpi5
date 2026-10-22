/*
 * startup_gain.h — the startup mute and fade, as one function of the frame count.
 *
 * STARTUP_MUTE_MS / STARTUP_FADE_MS hide what a card does when rbp first opens it
 * (the FLX4 thumps, docs/16). Until 2026-10-23 the gain was applied to the master
 * and monitor pairs only, inside flush_master()'s loop, while the headphone and
 * booth pairs are folded in afterwards from their own staged streams — so they
 * went out at full level from the first frame. The operator heard it: about half
 * a second of buzz in the headphones, and only there, every time rbp started.
 * Every pair now takes its gain from here, beside audioshim.c for the reason
 * mirror_policy.h gives (the shim cannot be linked into a test).
 *
 * t is the frame's index since the stream (re)started, counted at 44100 like
 * mute and fade. Returns 1.0 once t >= mute + fade, and also when mute is 0: the
 * mute is the switch, a fade without one never applies (as before).
 */
#ifndef STARTUP_GAIN_H
#define STARTUP_GAIN_H

static inline float startup_gain(unsigned long long t, unsigned long long mute,
                                 unsigned long long fade)
{
    if (mute == 0 || t >= mute + fade)
        return 1.0f;
    if (t < mute)
        return 0.0f;
    return (float)(t - mute) / (float)fade;    /* fade > 0 here */
}

#endif
