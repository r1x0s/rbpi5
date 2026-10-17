/*
 * test_beatsync.c -- pins beatsync_guard.c, the rule in front of rbp's
 * BeatSync::checkPrecision (the 2026-10-17 SIGSEGV at pc 0x7c404) and
 * BeatSync::syncBeat (the same unchecked cache, one call later).
 *
 * The fake is laid out the way rbp's objects are: a BeatSync whose +0 is the
 * player count and +4 the BeatSyncInfo array, 88-byte records with the Player*
 * at +0, the cached BeatPosInfo* at +4 and the request bytes at +24/+25 and
 * +48/+49, all as
 * 32-bit words. On a 64-bit host it is mapped below 4 GB (MAP_32BIT) so those
 * words can hold real addresses; on the target every address already fits.
 *
 * What a pass means: the guard leaves rbp alone in every state rbp handles
 * itself, fills the cache when the deck has a grid, and in the state that
 * crashed -- a sync slave with a request pending and no grid -- clears the
 * request and skips instead of letting rbp read 0x24. It says nothing about
 * whether rbp's own sync is then correct; that is the deck's job on the unit.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

#include "beatsync_guard.h"

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

#define NPLAYERS 2

static unsigned char *arena;          /* below 4 GB on every host */
static unsigned char *bs;             /* the BeatSync */
static unsigned char *info;           /* its BeatSyncInfo[NPLAYERS] */
static unsigned char *player[NPLAYERS];
static unsigned char *grid[NPLAYERS]; /* what getBeatPosInfo answers now */
static int beatpos_calls;

static uint32_t w(const void *p) { return (uint32_t)(uintptr_t)p; }

static void put32(void *base, unsigned off, uint32_t v)
{
    memcpy((char *)base + off, &v, 4);
}

static uint32_t get32(const void *base, unsigned off)
{
    uint32_t v;
    memcpy(&v, (const char *)base + off, 4);
    return v;
}

static unsigned char *rec(int i) { return info + i * BS_INFO_SIZE; }

static void *fake_beatpos(void *pl)
{
    beatpos_calls++;
    for (int i = 0; i < NPLAYERS; i++)
        if (pl == player[i])
            return grid[i];
    return NULL;
}

/* A fresh BeatSync: both players present, both caches NULL (as rbp's
 * constructor leaves them), no request pending, every deck with a grid. */
static void reset(void)
{
    memset(arena, 0, 65536);
    bs = arena;
    info = arena + 64;
    for (int i = 0; i < NPLAYERS; i++) {
        player[i] = arena + 4096 + i * 4096;
        grid[i] = arena + 32768 + i * 4096;
        put32(rec(i), BS_INFO_PLAYER_OFF, w(player[i]));
    }
    put32(bs, BS_COUNT_OFF, NPLAYERS);
    put32(bs, BS_INFO_OFF, w(info));
    beatpos_calls = 0;
}

static void cache(int i, void *p) { put32(rec(i), BS_INFO_BEATPOS_OFF, w(p)); }
static uint32_t cached(int i) { return get32(rec(i), BS_INFO_BEATPOS_OFF); }
static void request(int i, int f) { rec(i)[BS_PENDING_OFF(BSG_FN_CHECKPREC, f)] = 1; }
static int pending(int i, int f) { return rec(i)[BS_PENDING_OFF(BSG_FN_CHECKPREC, f)]; }
static void request_sb(int i, int f) { rec(i)[BS_PENDING_OFF(BSG_FN_SYNCBEAT, f)] = 1; }
static int pending_sb(int i, int f) { return rec(i)[BS_PENDING_OFF(BSG_FN_SYNCBEAT, f)]; }

static void test_layout(void)
{
    /* The offsets are rbp's, read off checkPrecision; pin them so an edit to
     * the header is an edit to this test as well. */
    CHECK(BS_INFO_SIZE == 88, "record size %d", BS_INFO_SIZE);
    CHECK(BS_PENDING_OFF(BSG_FN_CHECKPREC, 0) == 25, "checkPrecision flag 0 at %d",
          BS_PENDING_OFF(BSG_FN_CHECKPREC, 0));
    CHECK(BS_PENDING_OFF(BSG_FN_CHECKPREC, 1) == 49, "checkPrecision flag 1 at %d",
          BS_PENDING_OFF(BSG_FN_CHECKPREC, 1));
    CHECK(BS_PENDING_OFF(BSG_FN_SYNCBEAT, 0) == 24, "syncBeat flag 0 at %d",
          BS_PENDING_OFF(BSG_FN_SYNCBEAT, 0));
    CHECK(BS_PENDING_OFF(BSG_FN_SYNCBEAT, 1) == 48, "syncBeat flag 1 at %d",
          BS_PENDING_OFF(BSG_FN_SYNCBEAT, 1));
    CHECK(BS_INFO_BEATPOS_OFF == 4, "cache at %d", BS_INFO_BEATPOS_OFF);
}

/* The state in the owner's crash.log: deck index 1 (DECK 2) checked against
 * master 0 with flag 0, a request pending, its cache NULL and no grid. */
static void test_the_crash(void)
{
    int which = -1;
    reset();
    cache(0, grid[0]);
    grid[1] = NULL;
    request(1, 0);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, &which) == BSG_SKIP,
          "the crash state must be skipped");
    CHECK(which == 1, "the missing grid is deck index 1, got %d", which);
    CHECK(pending(1, 0) == 0, "the request must be consumed, as rbp does");
    CHECK(cached(1) == 0, "nothing may be written into a NULL cache");
    CHECK(cached(0) == w(grid[0]), "the master's cache is left alone");
}

/* A deck that HAS a grid but whose cache was never filled: filled, and rbp
 * runs -- beat sync keeps working instead of being skipped. */
static void test_refill(void)
{
    int which = -1;
    reset();
    cache(0, grid[0]);
    request(1, 0);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, &which) == BSG_REFILLED,
          "a deck with a grid must be refilled, not skipped");
    CHECK(which == 1, "refilled deck index 1, got %d", which);
    CHECK(cached(1) == w(grid[1]), "cache holds the live grid");
    CHECK(pending(1, 0) == 1, "the request is rbp's to consume");

    /* The master's NULL cache is filled too (rbp reads it first, 0x7c30c). */
    reset();
    cache(1, grid[1]);
    request(1, 1);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 1, fake_beatpos, &which) == BSG_REFILLED,
          "master refill");
    CHECK(which == 0 && cached(0) == w(grid[0]), "master's cache filled");
}

/* No grid on the MASTER: skipped, the slave's request consumed. */
static void test_master_without_grid(void)
{
    int which = -1;
    reset();
    cache(1, grid[1]);
    grid[0] = NULL;
    request(1, 1);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 1, fake_beatpos, &which) == BSG_SKIP,
          "master without a grid is skipped");
    CHECK(which == 0, "the missing grid is the master's, got %d", which);
    CHECK(pending(1, 1) == 0, "flag 1's request consumed");
}

/* Every state rbp copes with on its own: the guard must not touch it. */
static void test_hands_off(void)
{
    unsigned char before[65536];

    /* Normal sync: both caches present. Not a byte changes, no call made. */
    reset();
    cache(0, grid[0]);
    cache(1, grid[1]);
    request(1, 0);
    memcpy(before, arena, sizeof before);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, NULL) == BSG_RUN, "normal");
    CHECK(memcmp(before, arena, sizeof before) == 0, "normal: memory unchanged");
    CHECK(beatpos_calls == 0, "normal: getBeatPosInfo not called");

    /* No request pending, even with NULL caches: rbp returns at once. */
    reset();
    memcpy(before, arena, sizeof before);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, NULL) == BSG_RUN, "idle");
    CHECK(memcmp(before, arena, sizeof before) == 0, "idle: unchanged");

    /* A request on the OTHER flag does not count for this one. */
    reset();
    grid[1] = NULL;
    request(1, 1);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, NULL) == BSG_RUN,
          "flag 0 call with only flag 1 pending");
    CHECK(pending(1, 1) == 1, "flag 1's request untouched");

    /* Out of range, deck == master, bad flag, no record array, no master
     * player: all rbp's own early returns. */
    reset();
    grid[1] = NULL;
    request(1, 0);
    memcpy(before, arena, sizeof before);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 2, 0, 0, fake_beatpos, NULL) == BSG_RUN, "deck 2");
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, -1, 0, 0, fake_beatpos, NULL) == BSG_RUN, "deck -1");
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 5, 0, fake_beatpos, NULL) == BSG_RUN, "master 5");
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 1, 0, fake_beatpos, NULL) == BSG_RUN, "deck==master");
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 2, fake_beatpos, NULL) == BSG_RUN, "flag 2");
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, NULL, 1, 0, 0, fake_beatpos, NULL) == BSG_RUN, "no bs");
    CHECK(memcmp(before, arena, sizeof before) == 0, "early returns: unchanged");
    put32(bs, BS_INFO_OFF, 0);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, NULL) == BSG_RUN, "no array");
    put32(bs, BS_INFO_OFF, w(info));
    put32(rec(0), BS_INFO_PLAYER_OFF, 0);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, NULL) == BSG_RUN,
          "no master player");
    CHECK(pending(1, 0) == 1, "no master player: request is rbp's");
}

/* A slave with no player at all (never seen, but rbp would call
 * getSyncTempo(NULL) on it): skipped rather than refilled from nothing. */
static void test_no_slave_player(void)
{
    reset();
    cache(0, grid[0]);
    put32(rec(1), BS_INFO_PLAYER_OFF, 0);
    request(1, 0);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, NULL) == BSG_SKIP,
          "no slave player");
    CHECK(beatpos_calls == 0, "getBeatPosInfo(NULL) never called");
    /* And no callback at all is a skip, never a crash. */
    reset();
    cache(0, grid[0]);
    request(1, 0);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, NULL, NULL) == BSG_SKIP, "no callback");
}

/* syncBeat: the same rule on its own request byte, and the two requests are
 * independent of each other. */
static void test_syncbeat(void)
{
    int which = -1;

    /* A gridless track loaded onto a synced deck: syncBeat's request stands,
     * the deck's cache was written back to NULL. Skipped, request dropped. */
    reset();
    cache(0, grid[0]);
    grid[1] = NULL;
    request_sb(1, 1);
    CHECK(beatsync_guard(BSG_FN_SYNCBEAT, bs, 1, 0, 1, fake_beatpos, &which)
          == BSG_SKIP, "syncBeat without a grid is skipped");
    CHECK(which == 1 && pending_sb(1, 1) == 0, "syncBeat request consumed");

    /* With a grid: refilled, and the request is left for rbp. */
    reset();
    cache(0, grid[0]);
    request_sb(1, 0);
    CHECK(beatsync_guard(BSG_FN_SYNCBEAT, bs, 1, 0, 0, fake_beatpos, &which)
          == BSG_REFILLED, "syncBeat refill");
    CHECK(cached(1) == w(grid[1]) && pending_sb(1, 0) == 1, "syncBeat refilled");

    /* A checkPrecision request is not a syncBeat request, and back. */
    reset();
    grid[1] = NULL;
    request(1, 0);
    CHECK(beatsync_guard(BSG_FN_SYNCBEAT, bs, 1, 0, 0, fake_beatpos, NULL)
          == BSG_RUN, "syncBeat ignores checkPrecision's byte");
    CHECK(pending(1, 0) == 1, "checkPrecision's byte untouched by syncBeat");
    reset();
    grid[1] = NULL;
    request_sb(1, 0);
    CHECK(beatsync_guard(BSG_FN_CHECKPREC, bs, 1, 0, 0, fake_beatpos, NULL)
          == BSG_RUN, "checkPrecision ignores syncBeat's byte");
    CHECK(pending_sb(1, 0) == 1, "syncBeat's byte untouched by checkPrecision");

    /* An unknown function selector is left alone. */
    reset();
    grid[1] = NULL;
    request(1, 0);
    CHECK(beatsync_guard(7, bs, 1, 0, 0, fake_beatpos, NULL) == BSG_RUN,
          "unknown fn");
}

int main(void)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__x86_64__) && defined(MAP_32BIT)
    flags |= MAP_32BIT;
#endif
    arena = mmap(NULL, 65536, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (arena != MAP_FAILED && (uintptr_t)arena + 65536 > 0xffffffffull) {
        /* A 64-bit host without MAP_32BIT (aarch64): ask for a low address. */
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
        munmap(arena, 65536);
        arena = MAP_FAILED;
        for (uintptr_t a = 0x10000000u; a < 0xf0000000u && arena == MAP_FAILED;
             a += 0x10000000u)
            arena = mmap((void *)a, 65536, PROT_READ | PROT_WRITE,
                         flags | MAP_FIXED_NOREPLACE, -1, 0);
    }
    if (arena == MAP_FAILED || (uintptr_t)arena + 65536 > 0xffffffffull) {
        printf("test_beatsync: cannot map the fake below 4 GB\n");
        return 1;
    }
    test_layout();
    test_the_crash();
    test_refill();
    test_master_without_grid();
    test_hands_off();
    test_no_slave_player();
    test_syncbeat();
    printf("test_beatsync: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
