/*
 * beatsync_guard.c -- the rule in front of rbp's BeatSync::checkPrecision
 * and BeatSync::syncBeat.
 * The account of the crash and of every branch is in beatsync_guard.h.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "beatsync_guard.h"

static uint32_t rd32(const void *base, unsigned off)
{
     uint32_t v;
     memcpy(&v, (const char *)base + off, sizeof v);
     return v;
}

static void wr32(void *base, unsigned off, uint32_t v)
{
     memcpy((char *)base + off, &v, sizeof v);
}

static void *rdptr(const void *base, unsigned off)
{
     return (void *)(uintptr_t)rd32(base, off);
}

/* 1 = this record's cache is usable (it was, or now is, non-NULL). */
static int fill(unsigned char *info, void *(*beatpos)(void *), int *filled)
{
     void *player, *bp;

     if (rdptr(info, BS_INFO_BEATPOS_OFF))
          return 1;
     player = rdptr(info, BS_INFO_PLAYER_OFF);
     if (!player || !beatpos)
          return 0;
     bp = beatpos(player);
     if (!bp)
          return 0;
     wr32(info, BS_INFO_BEATPOS_OFF, (uint32_t)(uintptr_t)bp);
     *filled = 1;
     return 1;
}

int beatsync_guard(int fn, void *bs, int deck, int master, int flag,
                   void *(*beatpos)(void *player), int *which)
{
     unsigned char *base, *di, *mi;
     int n, req, filled_d = 0, filled_m = 0;

     if (!bs || flag < 0 || flag > 1 ||
         (fn != BSG_FN_CHECKPREC && fn != BSG_FN_SYNCBEAT))
          return BSG_RUN;
     req = BS_PENDING_OFF(fn, flag);
     n = (int)rd32(bs, BS_COUNT_OFF);
     base = rdptr(bs, BS_INFO_OFF);
     /* Everything rbp itself bails out on, or that this rule cannot vouch for,
      * is left to rbp exactly as it was. */
     if (!base || deck < 0 || deck >= n || master < 0 || master >= n ||
         deck == master)
          return BSG_RUN;
     di = base + (size_t)deck * BS_INFO_SIZE;
     mi = base + (size_t)master * BS_INFO_SIZE;
     if (!di[req])
          return BSG_RUN;                  /* rbp returns at once */
     if (!rdptr(mi, BS_INFO_PLAYER_OFF))
          return BSG_RUN;                  /* checkPrecision returns at 0x7c270 */

     /* Master first: it is the one both functions dereference first
      * (checkPrecision 0x7c30c, syncBeat 0x7b434). */
     if (!fill(mi, beatpos, &filled_m)) {
          di[req] = 0;
          if (which)
               *which = master;
          return BSG_SKIP;
     }
     if (!fill(di, beatpos, &filled_d)) {
          di[req] = 0;
          if (which)
               *which = deck;
          return BSG_SKIP;
     }
     if (filled_m || filled_d) {
          if (which)
               *which = filled_d ? deck : master;
          return BSG_REFILLED;
     }
     return BSG_RUN;
}
