/*
 * hp_align.c -- the headphone delay and the alignment probe. See hp_align.h.
 */
#include <math.h>
#include <string.h>

#include "hp_align.h"

/* ---- the delay ------------------------------------------------------------ */

unsigned hp_delay_init(struct hp_delay *dl, long frames)
{
    if (frames < 0)
        frames = 0;
    if (frames > HP_DELAY_MAX)
        frames = HP_DELAY_MAX;
    memset(dl->ring, 0, sizeof dl->ring);
    dl->d = (unsigned)frames;
    dl->pos = 0;
    return dl->d;
}

void hp_delay_run(struct hp_delay *dl, int32_t *buf, unsigned frames,
                  unsigned stride, unsigned a, unsigned b, int invert)
{
    unsigned i;

    if (dl->d == 0 && !invert)
        return;
    for (i = 0; i < frames; i++) {
        int32_t *fa = &buf[i * stride + a];
        int32_t *fb = &buf[i * stride + b];
        int32_t l = *fa, r = *fb;

        if (dl->d) {
            int32_t *slot = &dl->ring[dl->pos * 2];
            int32_t ol = slot[0], or_ = slot[1];

            slot[0] = l;
            slot[1] = r;
            if (++dl->pos == dl->d)
                dl->pos = 0;
            l = ol;
            r = or_;
        }
        if (invert) {
            /* -(-2^23) is 2^23, one past the 24-bit range, and s24pack.c keeps
             * only bits 23..0 -- it would come out as -2^23 again, a full-scale
             * click. So the one value without a negative twin saturates. */
            l = (l <= -(1 << 23)) ? (1 << 23) - 1 : -l;
            r = (r <= -(1 << 23)) ? (1 << 23) - 1 : -r;
        }
        *fa = l;
        *fb = r;
    }
}

/* ---- the probe ------------------------------------------------------------ */

void hp_probe_reset(struct hp_probe *pr)
{
    pr->fill = 0;
    pr->computing = 0;
    pr->next_lag = -HP_PROBE_MAX_LAG;
    pr->pe = 0;
    pr->best_score = 0.0;
    pr->best_lag = 0;
    pr->c0 = 0;
}

/* (L + R) of two 24-bit samples, down to 16 bits. */
static inline int16_t mono16(int32_t l, int32_t r)
{
    int32_t v = (l + r) >> 9;

    if (v > 32767)
        v = 32767;
    if (v < -32768)
        v = -32768;
    return (int16_t)v;
}

/* The window is full: the per-lag energies and the headphone energy, once. */
static void probe_arm(struct hp_probe *pr)
{
    unsigned i;
    int64_t s = 0;

    pr->msq[0] = 0;
    for (i = 0; i < HP_PROBE_HIST; i++) {
        s += (int32_t)pr->m[i] * (int32_t)pr->m[i];
        pr->msq[i + 1] = s;
    }
    s = 0;
    for (i = 0; i < HP_PROBE_WIN; i++) {
        int32_t v = pr->p[HP_PROBE_MAX_LAG + i];
        s += v * v;
    }
    pr->pe = s;
    pr->computing = 1;
    pr->next_lag = -HP_PROBE_MAX_LAG;
    pr->best_score = 0.0;
    pr->best_lag = 0;
    pr->c0 = 0;
}

void hp_probe_feed(struct hp_probe *pr, const int32_t *buf, unsigned frames,
                   unsigned stride, unsigned ma, unsigned mb,
                   unsigned pa, unsigned pb)
{
    unsigned i;

    if (pr->computing)
        return;
    for (i = 0; i < frames && pr->fill < HP_PROBE_HIST; i++) {
        const int32_t *f = &buf[i * stride];

        pr->m[pr->fill] = mono16(f[ma], f[mb]);
        pr->p[pr->fill] = mono16(f[pa], f[pb]);
        pr->fill++;
    }
    if (pr->fill == HP_PROBE_HIST)
        probe_arm(pr);
}

/* The correlation sum at one lag: p's window against m shifted by `lag'. */
static int64_t corr_at(const struct hp_probe *pr, int lag)
{
    const int16_t *p = &pr->p[HP_PROBE_MAX_LAG];
    const int16_t *m = &pr->m[HP_PROBE_MAX_LAG + lag];
    int64_t c = 0;
    unsigned i;

    for (i = 0; i < HP_PROBE_WIN; i++)
        c += (int32_t)p[i] * (int32_t)m[i];
    return c;
}

static int64_t energy_at(const struct hp_probe *pr, int lag)
{
    unsigned lo = (unsigned)(HP_PROBE_MAX_LAG + lag);

    return pr->msq[lo + HP_PROBE_WIN] - pr->msq[lo];
}

static int milli(int64_t c, int64_t pe, int64_t e)
{
    double den;

    if (pe <= 0 || e <= 0)
        return 0;
    den = sqrt((double)pe * (double)e);
    if (den <= 0.0)
        return 0;
    return (int)lrint(1000.0 * (double)c / den);
}

int hp_probe_step(struct hp_probe *pr, int nlags, struct hp_probe_result *out)
{
    int n;
    int64_t min_e = (int64_t)HP_PROBE_MIN_MS * HP_PROBE_WIN;

    if (!pr->computing)
        return 0;
    for (n = 0; n < nlags && pr->next_lag <= HP_PROBE_MAX_LAG; n++) {
        int lag = pr->next_lag++;
        int64_t c = corr_at(pr, lag);
        int64_t e = energy_at(pr, lag);
        double score, mag, best_mag;

        if (lag == 0)
            pr->c0 = c;
        if (e <= 0)
            continue;
        /* corr^2 = C^2 / (pe * E): pe is the same for every lag, so C*|C|/E
         * ranks the lags by |corr| and keeps the sign. Double, because C^2
         * passes 2^63; it is one division per lag, not per sample. */
        score = (double)c * (double)(c < 0 ? -c : c) / (double)e;
        mag = score < 0 ? -score : score;
        best_mag = pr->best_score < 0 ? -pr->best_score : pr->best_score;
        if (mag > best_mag) {
            pr->best_score = score;
            pr->best_lag = lag;
        }
    }
    if (pr->next_lag <= HP_PROBE_MAX_LAG)
        return 0;

    /* Done: report, and start collecting the next window. */
    {
        int64_t e0 = energy_at(pr, 0);
        int64_t eb = energy_at(pr, pr->best_lag);

        out->valid = (pr->pe >= min_e && e0 >= min_e);
        out->lag = pr->best_lag;
        out->corr_milli = milli(corr_at(pr, pr->best_lag), pr->pe, eb);
        out->corr0_milli = milli(pr->c0, pr->pe, e0);
    }
    pr->computing = 0;
    pr->fill = 0;
    return 1;
}
