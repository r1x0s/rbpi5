/*
 * beatsync_guard.h -- keep rbp's BeatSync::checkPrecision from following a NULL.
 *
 * WHAT CRASHED (the owner's crash.log, 2026-10-17, twice, identical):
 *
 *   SIGSEGV addr=0x00000024 pc=0x0007c404 lr=0x0007c52c
 *   r2=0xf2bd0fe0 r3=0 r4=0xc3efec88 r5=0xf2bd0f88 r6=0xf2bd0f68 r7=0xc3efec8e
 *
 * pc 0x7c404 is inside playengine::BeatSync::checkPrecision(int const& deck,
 * int const& master, bool const& flag) @0x7c1f0, in rbp's own code (rbp is not
 * PIE, and the binary in the tarball is byte-identical to the one that ran
 * before the upstream sync, work/rbp-16). The instructions are
 *
 *   7c3fc  ldr r2, [sp, #12]      r2 = &info[deck]          (0xf2bd0fe0)
 *   7c400  ldr r3, [r2, #4]       r3 = info[deck].beatpos   (NULL)
 *   7c404  ldr r2, [r3, #36]      <- fault, 0 + 0x24
 *
 * and the registers say which deck: r6 is the BeatSync, r5 = *(r6+4) is its
 * vector of 88-byte BeatSyncInfo records, and r2 - r5 = 88, so deck index 1
 * (DECK 2), checked against the sync master, with flag 0 (r7 = caller sp + 6,
 * which checkAllPrecision @0x7c8b8 fills with 0 for its first call).
 *
 * WHY THE POINTER IS NULL. BeatSyncInfo+4 is a CACHE of
 * Player::getBeatPosInfo() @0x684dc (the loaded track's beat grid: NULL when
 * the deck's reader has no beat position, i.e. no track or no grid). Only
 * checkStartCondition @0x7b0b4 fills it, and it NULL-checks before going on.
 * But the "check precision" request bytes, info+25 (flag 0) and info+49
 * (flag 1), are set by SyncInfoMgr::updateSlaveStates @0x7feb8 on a deck's
 * state change, independently of that cache -- and checkPrecision consumes the
 * request and then dereferences the cache with no test at all (twice: the
 * master's at 0x7c30c, the deck's at 0x7c400). A sync slave whose cache was
 * never filled (it starts at 0 in BeatSync's constructor @0x7d880), or was
 * filled while its deck had no grid, therefore takes rbp down the first time
 * a state change (PLAY) asks for a precision check on it.
 *
 * THE SAME HOLE ONE CALL LATER. BeatSync::syncBeat @0x7b30c (called from
 * BeatSync::sync @0x7d0f4 on every engine pass) has checkPrecision's shape and
 * checkPrecision's flaw: its request is byte +0 of the same 24-byte block (set by
 * startBeatSync @0x7af58), and it reads the master's cache at 0x7b434 and the
 * deck's after it with no test. startBeatSync is only reached once both caches
 * were non-NULL, but a later checkStartCondition that bails (a gridless track
 * loaded onto a synced deck) writes NULL back over one of them while the
 * request stands. So the guard sits in front of both functions; BSG_FN_* says
 * which, and the only difference is which request byte it looks at.
 *
 * WHAT THE GUARD DOES. It runs in front of the function, in rbp's own thread,
 * and only ever acts when rbp is about to fault:
 *
 *   - no request pending for this deck/flag, indices out of range, deck ==
 *     master, no master player: rbp bails out on these itself, so the
 *     original runs untouched (BSG_RUN);
 *   - both caches non-NULL: the original runs untouched (BSG_RUN) -- the
 *     normal case, and the guard writes nothing;
 *   - a NULL cache whose deck HAS a grid now: the cache is filled with exactly
 *     the value checkStartCondition would have stored, and the original runs,
 *     so beat sync keeps working (BSG_REFILLED);
 *   - a NULL cache whose deck has NO grid: the request byte is cleared -- which
 *     is the first thing checkPrecision itself does with it -- and the call is
 *     skipped, the same outcome as one of rbp's own early returns (BSG_SKIP;
 *     a skipped syncBeat returns 0, its own "nothing done" at 0x7b878).
 *
 * This file is pure: no rbp addresses, no logging, no allocation. The live
 * Player::getBeatPosInfo is passed in, which is what lets test_beatsync pin the
 * rules on a host against a fake BeatSync. rbp_bridge.c installs the two hooks.
 *
 * rbp's objects are 32-bit and their pointers are 32-bit words, so the layout
 * below is read as 32-bit words and converted, never as `void *` fields: that
 * keeps the offsets right on a 64-bit host too (the test maps its fake below
 * 4 GB there).
 */
#ifndef RBPI4B_BEATSYNC_GUARD_H
#define RBPI4B_BEATSYNC_GUARD_H

/* playengine::BeatSync (r0 of checkPrecision): +0 the player count (int),
 * +4 the BeatSyncInfo vector's begin. Read off BeatSync::BeatSync @0x7d880 and
 * checkPrecision's own range check @0x7c250. */
#define BS_COUNT_OFF        0
#define BS_INFO_OFF         4

/* playengine::BeatSync::BeatSyncInfo, 88 bytes (the vector's stride, `mov r1,
 * #88` @0x7c208; _M_fill_insert's 0x2e8ba2e8 divisor): +0 Player*, +4 the
 * cached BeatPosInfo*, and per flag f a 24-byte block at 24*(f+1) whose byte +0
 * is syncBeat's request (syncBeat @0x7b31c..0x7b348; set by startBeatSync
 * @0x7b004) and byte +1 checkPrecision's (checkPrecision @0x7c218..0x7c244; set
 * by SyncInfoMgr::updateSlaveStates, `strb [r9, #25]`/`#49`). */
#define BS_INFO_SIZE        88
#define BS_INFO_PLAYER_OFF  0
#define BS_INFO_BEATPOS_OFF 4
#define BS_BLOCK_OFF(f)     (24 * ((f) + 1))

/* Which of rbp's functions the call is in front of: the value is that
 * function's request byte within the flag's block. */
#define BSG_FN_SYNCBEAT     0
#define BSG_FN_CHECKPREC    1
#define BS_PENDING_OFF(fn, f) (BS_BLOCK_OFF(f) + (fn))

enum {
     BSG_RUN = 0,       /* call rbp's checkPrecision; nothing was touched */
     BSG_REFILLED = 1,  /* a NULL cache was filled from the live player; call it */
     BSG_SKIP = 2,      /* no grid to check against: request cleared, do NOT call */
};

/* Decide one call of `fn` (BSG_FN_CHECKPREC or BSG_FN_SYNCBEAT) with
 * (deck, master, flag) on BeatSync `bs`. `beatpos` is Player::getBeatPosInfo;
 * it is called only for a NULL cache. On BSG_SKIP, *which (if non-NULL) is set
 * to the deck index whose grid is missing; on BSG_REFILLED, to the one that was
 * filled. */
int beatsync_guard(int fn, void *bs, int deck, int master, int flag,
                   void *(*beatpos)(void *player), int *which);

#endif /* RBPI4B_BEATSYNC_GUARD_H */
