/*
 * test_align.c -- pins hp_align.c: the headphone delay and the alignment probe.
 *
 * The probe's whole value is its SIGN and its arithmetic, and neither can be read
 * off the unit without already trusting it, so both are pinned here against
 * synthetic signals whose offset is known: a master pair carrying noise, and a
 * headphone pair carrying the same noise shifted by D frames, fed in the shim's
 * own 64-frame blocks with the shim's own 16 lags per block.
 *
 * What a pass means: the module measures and delays what the header says it
 * does. It says nothing about how far apart rbp's streams really are; that is
 * the probe's job on the unit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hp_align.h"

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

#define CH 4            /* the GRV6's frame: master 0/1, headphones 2/3 */
#define BLOCK 64        /* rbp's block, measured (docs/09) */
#define LAGS_PER_BLOCK 8         /* audioshim.c's ALIGN_LAGS_PER_BLOCK */

/* ---- a deterministic 24-bit noise source --------------------------------- */
static uint32_t rng = 1;
static int32_t noise24(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (int32_t)(rng >> 8) - (1 << 23);         /* -2^23 .. 2^23-1 */
}

#define SIG_LEN (HP_PROBE_HIST * 4 + 8192)
static int32_t sig[SIG_LEN];
static int32_t other[SIG_LEN];

static void make_signals(uint32_t seed)
{
    rng = seed;
    for (int i = 0; i < SIG_LEN; i++)
        sig[i] = noise24() / 2;                     /* -6 dBFS: room for sums */
    for (int i = 0; i < SIG_LEN; i++)
        other[i] = noise24() / 4;
}

/* The sample at t, or silence outside the signal. */
static int32_t at(const int32_t *s, long t)
{
    return (t >= 0 && t < SIG_LEN) ? s[t] : 0;
}

/*
 * Build block `blk' of a frame where the master carries sig[t] (+ `mix_other'
 * of an unrelated signal, i.e. the other deck) and the headphones carry
 * sig[t + shift] * hp_sign (+ nothing else). shift > 0: the headphones are
 * early by `shift' frames.
 */
static void build_block(int32_t *frame, long blk, int shift, int hp_sign,
                        int mix_other, int hp_silent)
{
    for (int i = 0; i < BLOCK; i++) {
        long t = blk * BLOCK + i + 2048;            /* start inside the signal */
        int32_t m = at(sig, t) + (mix_other ? at(other, t) : 0);
        int32_t p = hp_silent ? 0 : at(sig, t + shift) * hp_sign;

        frame[i * CH + 0] = m;
        frame[i * CH + 1] = m;
        frame[i * CH + 2] = p;
        frame[i * CH + 3] = p;
    }
}

/* Run the probe over blocks until it reports; returns the blocks it took. */
static int run_probe(int shift, int hp_sign, int mix_other, int hp_silent,
                     struct hp_delay *dl, struct hp_probe_result *res)
{
    static struct hp_probe pr;
    static int32_t frame[BLOCK * CH];

    hp_probe_reset(&pr);
    for (long blk = 0; blk < 100000; blk++) {
        build_block(frame, blk, shift, hp_sign, mix_other, hp_silent);
        if (dl)
            hp_delay_run(dl, frame, BLOCK, CH, 2, 3, 0);
        hp_probe_feed(&pr, frame, BLOCK, CH, 0, 1, 2, 3);
        if (hp_probe_step(&pr, LAGS_PER_BLOCK, res))
            return (int)blk + 1;
    }
    return -1;
}

static void check_probe_shift(int shift)
{
    struct hp_probe_result r;
    int blocks = run_probe(shift, 1, 0, 0, NULL, &r);

    CHECK(blocks > 0, "shift %d: the probe never reported", shift);
    CHECK(r.valid, "shift %d: reported invalid on a full-level signal", shift);
    CHECK(r.lag == shift, "shift %d: lag %d", shift, r.lag);
    CHECK(r.corr_milli >= 950, "shift %d: corr %d/1000 at the best lag",
          shift, r.corr_milli);
    if (shift != 0)
        CHECK(r.corr0_milli < 200 && r.corr0_milli > -200,
              "shift %d: noise correlated %d/1000 at lag 0", shift,
              r.corr0_milli);
}

static void test_probe(void)
{
    struct hp_probe_result r;
    int blocks;

    make_signals(12345);

    /* the sign convention, both ways, and the edges of the search */
    check_probe_shift(0);
    check_probe_shift(37);
    check_probe_shift(-13);
    check_probe_shift(1);
    check_probe_shift(-1);
    check_probe_shift(HP_PROBE_MAX_LAG);
    check_probe_shift(-HP_PROBE_MAX_LAG);

    /* the cost: filling the window, then 2049 lags at 8 a block */
    blocks = run_probe(5, 1, 0, 0, NULL, &r);
    {
        int fill = (HP_PROBE_HIST + BLOCK - 1) / BLOCK;
        int comp = (2 * HP_PROBE_MAX_LAG + 1 + LAGS_PER_BLOCK - 1) / LAGS_PER_BLOCK;
        CHECK(blocks == fill + comp - 1 || blocks == fill + comp,
              "the first report took %d blocks, expected ~%d (fill %d + %d)",
              blocks, fill + comp, fill, comp);
    }

    /* a polarity flip is a NEGATIVE peak at the right lag, not a miss */
    run_probe(21, -1, 0, 0, NULL, &r);
    CHECK(r.valid && r.lag == 21, "inverted: lag %d (valid %d)", r.lag, r.valid);
    CHECK(r.corr_milli <= -950, "inverted: corr %d/1000, expected ~-1000",
          r.corr_milli);

    /* the realistic case: the master also carries the other deck */
    run_probe(-77, 1, 1, 0, NULL, &r);
    CHECK(r.valid && r.lag == -77, "with the other deck in the master: lag %d",
          r.lag);
    CHECK(r.corr_milli >= 700, "with the other deck in the master: corr %d/1000",
          r.corr_milli);

    /* silence on the headphones is not evidence */
    run_probe(0, 1, 0, 1, NULL, &r);
    CHECK(!r.valid, "a silent headphone pair was reported valid (lag %d corr %d)",
          r.lag, r.corr_milli);

    /* closed loop: delay the early headphones by what the probe said, and the
     * residual is zero. This is the procedure docs/09 gives the operator. */
    {
        static struct hp_delay dl;
        int shift = 37;

        run_probe(shift, 1, 0, 0, NULL, &r);
        CHECK(hp_delay_init(&dl, r.lag) == (unsigned)shift,
              "delay set to %d", r.lag);
        run_probe(shift, 1, 0, 0, &dl, &r);
        CHECK(r.valid && r.lag == 0, "after the delay: residual lag %d", r.lag);
        CHECK(r.corr0_milli >= 950, "after the delay: corr at lag 0 %d/1000",
              r.corr0_milli);
    }
}

/* ---- the delay ------------------------------------------------------------ */

static void test_delay(void)
{
    static struct hp_delay dl;
    int32_t buf[BLOCK * 2 * CH];
    int i;

    /* clamping */
    CHECK(hp_delay_init(&dl, -5) == 0, "a negative delay was not clamped to 0");
    CHECK(hp_delay_init(&dl, HP_DELAY_MAX + 1) == HP_DELAY_MAX,
          "a delay past the ring was not clamped");

    /* d = 0, no invert: a no-op */
    hp_delay_init(&dl, 0);
    for (i = 0; i < BLOCK * CH; i++)
        buf[i] = i * 3 - 7;
    hp_delay_run(&dl, buf, BLOCK, CH, 2, 3, 0);
    for (i = 0; i < BLOCK * CH; i++)
        if (buf[i] != i * 3 - 7)
            break;
    CHECK(i == BLOCK * CH, "delay 0 changed sample %d", i);

    /* an impulse, delayed across a block boundary, on the pair only */
    hp_delay_init(&dl, 70);
    memset(buf, 0, sizeof buf);
    buf[3 * CH + 0] = 111;      /* master: must stay where it is */
    buf[3 * CH + 2] = 222;
    buf[3 * CH + 3] = -333;
    hp_delay_run(&dl, buf, BLOCK, CH, 2, 3, 0);          /* block 1 */
    CHECK(buf[3 * CH + 0] == 111, "the master channel was touched");
    for (i = 0; i < BLOCK; i++)
        if (buf[i * CH + 2] || buf[i * CH + 3])
            break;
    CHECK(i == BLOCK, "the first 64 frames out were not silence (frame %d)", i);
    memset(buf, 0, sizeof buf);
    hp_delay_run(&dl, buf, BLOCK, CH, 2, 3, 0);          /* block 2 */
    /* frame 3 + 70 = 73 overall = frame 9 of block 2 */
    CHECK(buf[9 * CH + 2] == 222 && buf[9 * CH + 3] == -333,
          "the impulse came out as %d/%d at frame 73, expected 222/-333",
          buf[9 * CH + 2], buf[9 * CH + 3]);
    {
        int others = 0;
        for (i = 0; i < BLOCK; i++)
            if (i != 9 && (buf[i * CH + 2] || buf[i * CH + 3]))
                others++;
        CHECK(others == 0, "%d stray non-zero frames in block 2", others);
    }

    /* invert without a delay; -2^23 has no positive twin in 24 bits and must
     * saturate, not wrap (s24pack.c keeps bits 23..0 only) */
    hp_delay_init(&dl, 0);
    memset(buf, 0, sizeof buf);
    buf[0 * CH + 2] = -(1 << 23);
    buf[0 * CH + 3] = (1 << 23) - 1;
    buf[0 * CH + 1] = 5;
    hp_delay_run(&dl, buf, BLOCK, CH, 2, 3, 1);
    CHECK(buf[0 * CH + 2] == (1 << 23) - 1 && buf[0 * CH + 3] == -((1 << 23) - 1),
          "invert gave %d/%d", buf[0 * CH + 2], buf[0 * CH + 3]);
    CHECK(buf[0 * CH + 1] == 5, "invert touched the master");
}

int main(void)
{
    test_delay();
    test_probe();
    printf("test_align: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
