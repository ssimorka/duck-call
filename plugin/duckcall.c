/* DUCK CALL: a PercAll-style envelope FX for the OP-1 (hundekraft / kennel).
 * Each trigger fires a fixed decay envelope instead of following the source's loudness (ente is the compressor).
 *   blue   length (15 ms .. 2 s)                        press: attack, how fast the gain moves (0.2 .. 25 ms)
 *   green  strength: depth and decay together           press: accent (beat sources) / sensitivity (audio sources)
 *   white  shape: pump, linear, hold, choke             press: flip (gate .. flat .. duck)
 *   orange source, stepped and wrapping, beat first:
 *            1/1 1/2 1/4 1/8 1/16 1/4t 1/8t 1/16t   the tempo grid, no audio needed; accent makes the 1 hit hardest,
 *                                                   the other beats softer and the in-between pulses softest
 *            t1..t4     hits on a tape track (pre-fader), PercAll's choke
 *            tape, in   the same from the tape sum or the input
 *            self       onsets in the FX's own input (flip to gate: any sound becomes a hit)
 *            inst       the instrument bus (master FX only; elsewhere = self)
 * Flip blends the gain between duck (1 - f) and gate (f); the middle is flat. The attack slews both ways.
 * Strength works as PercAll's: a softer hit gives a shallower dip that also recovers sooner (constant decay ratio).
 * Audio hits quieter than the sensitivity are misses: they do not trigger, but the screen sees them.
 * The screen (below) is the carnival shooting gallery: it reads the hit feed (dc_t.ev) and the knobs.
 * Copyright (C) 2026 Steph Simorka. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
 * Source list and beat grid follow ente (op1r 52, pcppcp; kennel adaptation hundekraft), GPL-3.0-or-later. */
#include "op1_plugin.h"

enum { S_SELF, S_T1, S_T2, S_T3, S_T4, S_TAPE, S_IN, S_BEAT, S_INST };   /* T1..IN = OP1_SC_* numbers */
#define NDIV 8
#define NENT 16
#define E_SELF 14
static const uint8_t ent_src[NENT] = { S_BEAT, S_BEAT, S_BEAT, S_BEAT, S_BEAT, S_BEAT, S_BEAT, S_BEAT,
                                       S_T1, S_T2, S_T3, S_T4, S_TAPE, S_IN, S_SELF, S_INST };
static const uint8_t div_w[NDIV] = { 4, 2, 1, 1, 1, 2, 1, 1 };   /* window in beats   (1/1 1/2 1/4 1/8 1/16 1/4t 1/8t 1/16t) */
static const uint8_t div_k[NDIV] = { 1, 1, 1, 2, 4, 3, 3, 6 };   /* pulses per window */
static const uint8_t div_bar[NDIV] = { 1, 2, 4, 8, 16, 6, 12, 24 }; /* pulses per bar */
#define REARM 1323                       /* 30 ms between audio triggers */
#define PEAKWIN 441                      /* 10 ms to find a hit's strength */
#define FLOOR 48                         /* onsets below this level are not even misses */

enum { EV_DOWN = 1, EV_MISS = 2, EV_CHOKE = 4 };
typedef struct { uint16_t vel; uint8_t flags, pidx; } dc_ev_t;     /* vel Q15; pidx: pulse in the bar (beat sources) */

typedef struct {
    int32_t fast, slow;                  /* onset detector: level now and ~6 ms ago, |x| << 8 */
    int32_t hist[8];                     /* the follower every 32 samples */
    int32_t lng;
    int32_t pw, pv, pfire;               /* peak window: samples left, best velocity, fired */
    uint32_t tick; uint8_t hi;
    int32_t env;                         /* phase of the current hit, Q30 (1 at the hit .. 0) */
    int32_t depth;                       /* the hit's depth (strength x velocity), Q15 */
    int32_t dec;                         /* per-sample fall of the current hit, Q30 */
    int32_t g;                           /* smoothed gain, Q15 */
    int32_t rearm;
    uint32_t beats, pulses, hits;
    volatile uint32_t blocks;            /* render calls */
    volatile uint32_t sounding;          /* blocks with sound coming in or the tape running (the screen moves with this) */
    int32_t fph;                         /* our own beat phase while the tape stands still, 1/16384 samples */
    volatile int32_t shown;              /* envelope of the last block, Q15 (screen) */
    volatile int32_t gmin;               /* lowest gain since the screen last read it, Q15 (the screen resets it) */
    volatile uint32_t evseq;             /* hit feed for the screen: ev[evseq & 7] is the newest */
    dc_ev_t ev[8];
    volatile int8_t lit;                 /* beat sources: the pulse in the bar that last fired */
    int32_t cpk[3], crearm;              /* choke detector: the partner's block peaks, 30 ms re-arm */
} dc_t;
static dc_t st[3];

static inline int32_t word(const int16_t *p, int i) { return p[i] < 0 ? 0 : p[i]; }
static inline int ent_of(int32_t q15) { int e = (int)((q15 * NENT) >> 15); return e < 0 ? 0 : e >= NENT ? NENT - 1 : e; }
#ifdef OP1_HOST
__attribute__((weak)) volatile uint8_t op1_fx_wrap[3][4];
#endif

static void push_ev(dc_t *s, int32_t vel, int flags, int pidx)
{
    dc_ev_t *e = &s->ev[(s->evseq + 1) & 7];
    e->vel = (uint16_t)(vel < 0 ? 0 : vel > 32767 ? 32767 : vel); e->flags = (uint8_t)flags; e->pidx = (uint8_t)pidx;
    s->evseq = s->evseq + 1;                                       /* published after the data */
}

static void dc_init(int pos)
{
    dc_t *s = &st[pos % 3];
    for (int i = 0; i < 8; i++) s->hist[i] = 0;
    s->tick = 0; s->hi = 0; s->lng = 0; s->pw = s->pv = s->pfire = 0;
    s->fast = s->slow = s->env = s->rearm = 0; s->dec = 0; s->depth = 0; s->g = 32767; s->beats = 0; s->pulses = 0; s->hits = 0; s->fph = 0;
    s->shown = 0; s->gmin = 32767; s->lit = -1;
}

/* the beat count: from the tape position while the clock follows the tape (so the 1 is bar 1's), else our own */
static uint32_t beat_now(dc_t *s, const op1_beat_t *c)
{
    if (c->follows_tape && c->len > 0) return (uint32_t)((((int64_t)c->tape_pos) << 14) / c->len);
    return s->beats;
}

/* as ente: the sample of this block where a grid pulse of division d falls, or -1; *pidx = its pulse in the bar */
static int beat_pulse(dc_t *s, const op1_beat_t *c, int d, int *pidx)
{
    if (c->len < (256 << 14) || c->step <= 0 || c->next < 0 || c->next > c->len) return -1;
    const uint32_t B = beat_now(s, c);
    int64_t len = c->len, win = len * div_w[d], sub = win / div_k[d];
    int64_t e0 = (int64_t)(B % div_w[d]) * len + (len - c->next);
    if (e0 < 0) e0 += win;
    int64_t n0 = (e0 + sub - 1) / sub, t = n0 * sub;
    if (c->next < c->step) s->beats++;
    if (t >= e0 + c->step) return -1;
    const int wpb = div_w[d] >= 4 ? 1 : 4 / div_w[d];                 /* windows per bar */
    uint32_t w = B / div_w[d];
    int k = (int)n0;
    if (k >= div_k[d]) { k -= div_k[d]; w++; }
    *pidx = (int)(w % (uint32_t)wpb) * div_k[d] + k;
    return (int)((t - e0) * 64 / c->step);
}

/* audio sources: is the hit on the 1? (within an eighth of a beat of a bar line) */
static int on_the_one(dc_t *s, const op1_beat_t *c)
{
    if (c->len < (256 << 14)) return 0;
    if (c->follows_tape) {
        int64_t bar = (int64_t)c->len * 4, p = ((((int64_t)c->tape_pos) << 14) % bar);
        return p < c->len / 8 || p > bar - c->len / 8;
    }
    return (s->beats & 3) == 0 && (c->len - c->next) < c->len / 8;
}

/* shape of the dip over its phase ph (1 at the hit, falling straight to 0), Q15:
 *   0   pump:   ph^2, the classic sidechain swell (recovers fast, then eases in)
 *   1/3 linear: ph
 *   2/3 hold:   1-(1-ph)^2, stays down, then comes back
 *   1   choke:  1-(1-ph)^4, a cut that snaps open at the end (PercAll's choke) */
/* strength: 1 - (1 - knob)^2 from 1/8 to full, so the dip is deep from the middle of the knob up */
static inline int32_t strength_of(int32_t ks)
{
    const int32_t u = 32767 - ks, c = 32767 - ((u * u) >> 15);
    return 4096 + ((c * 28671) >> 15);
}
static inline int32_t shape_of(int32_t ph, int32_t sh)
{
    int32_t u = 32767 - ph, u2 = (u * u) >> 15, u4 = (u2 * u2) >> 15;
    int32_t pump = (ph * ph) >> 15, hold = 32767 - u2, choke = 32767 - u4;
    int32_t seg = sh * 3, i = seg >> 15, f = seg & 32767;
    if (i >= 3) return choke;
    int32_t a = i == 0 ? pump : i == 1 ? ph : hold, b = i == 0 ? ph : i == 1 ? hold : choke;
    return a + (((b - a) * f) >> 15);
}

/* PercAll's choke switches: with red pressed, the partner track's hits cut the current dip (or gate) short.
 * T1 and T2 choke each other, T3 and T4 choke each other; every other source is choked by T1. */
static inline int choke_of(int ent) { return ent == 8 ? 9 : ent == 9 ? 8 : ent == 10 ? 11 : ent == 11 ? 10 : 8; }

static void process(int pos, int16_t *l, int16_t *r, const int16_t *p)
{
    pos %= 3;
    dc_t *s = &st[pos];
    s->blocks++;
    int32_t kd = op1_fx_param_q15(p, 0), ks = op1_fx_param_q15(p, 1), kh = op1_fx_param_q15(p, 2);
    int32_t wa = word(p, 5), wsens = word(p, 6), wflip = word(p, 7);
    int32_t wg = 32767 - wflip;                                                      /* gate weight Q15: flip 32767 = duck, 0 = gate */
    int ent = ent_of(op1_fx_param_q15(p, 3)), src = ent_src[ent];
    if (src == S_INST && pos != OP1_FX_MASTER) src = S_SELF;
    const int hit = src == S_SELF;

    /* per block */
    int32_t dsamp = 661 + (int32_t)(((int64_t)((kd * kd) >> 15) * 87539) >> 15);     /* full-strength length: 15 ms .. 2 s */
    int32_t asamp = 9 + (((wa * wa) >> 15) * 4401 >> 15);                            /* attack: 0.2 .. 100 ms */
    int32_t ca = 32767 / asamp + 1;                                                  /* gain slew per sample, Q15 */
    int32_t thr = 64 + (((32767 - wsens) * 4000) >> 15);                            /* sensitivity: a hit's level must pass this */
    const int32_t sh = ((kh * 3 + 16383) >> 15) * 32767 / 3;                         /* the shape snaps to its four steps */

    op1_beat_t c;
    op1_beat_clock(&c);
    const int rolling = c.step > 0 && c.follows_tape;
    if (c.len >= (256 << 14) && c.step <= 0) {                                       /* tape stopped: the grid keeps the tempo on */
        c.next = c.len - s->fph; c.step = 0x100000; c.follows_tape = 0;             /* our own clock, so keys pump too */
        s->fph += 0x100000; while (s->fph >= c.len) s->fph -= c.len;
    } else s->fph = c.len > 0 ? (c.len - c.next) % c.len : 0;                       /* follow along, no jump when it stops */
    {
        int32_t pk = 0;
        for (int i = 0; i < OP1_FX_BLOCK; i += 4) { int32_t v = l[i] < 0 ? -l[i] : l[i]; if (v > pk) pk = v; if (r) { v = r[i] < 0 ? -r[i] : r[i]; if (v > pk) pk = v; } }
        if (rolling || pk > 96) s->sounding++;
    }
    static int16_t sc_mem[3][OP1_FX_BLOCK];
    const int16_t *x = 0;
    int pulse = -1, pidx = 0, pvel = 32767;
    if (src == S_BEAT) {
        const int d = ent;
        pulse = beat_pulse(s, &c, d, &pidx);
        if (pulse >= 0) {                                                            /* accent: the 1, the beats, the in-betweens */
            const int nb = div_bar[d], onbeat = (pidx * 4) % nb == 0;
            pvel = pidx == 0 ? 32767 : onbeat ? 32767 - ((wsens * 22937) >> 15) : 32767 - wsens;   /* at 100% only the 1 is left */
            s->pulses++;
            s->lit = (int8_t)pidx;
            push_ev(s, pvel, pidx == 0 ? EV_DOWN : 0, pidx);
        }
    } else {
        if (c.next < c.step) s->beats++;
        if (!hit) { op1_sc_source(src == S_INST ? OP1_SC_INST : src, sc_mem[pos]); x = sc_mem[pos]; }
        s->lit = -1;
    }

    int choke_at = -1;                                                 /* a choke this block: the sample it lands on */
    if (op1_fx_page[pos][3]) {
        static int16_t ck[3][OP1_FX_BLOCK];
        int32_t pk = 0, at = 0;
        if (op1_sc_source(ent_src[choke_of(ent)], ck[pos]))
            for (int i = 0; i < OP1_FX_BLOCK; i++) { int32_t v = ck[pos][i] < 0 ? -ck[pos][i] : ck[pos][i]; if (v > pk) { pk = v; at = i; } }
        if (s->crearm > 0) s->crearm -= OP1_FX_BLOCK;
        else if (pk > FLOOR * 4 && pk > 3 * s->cpk[2] && pk > 3 * s->cpk[1]) {   /* a jump out of a gap */
            choke_at = at; s->crearm = REARM;
            push_ev(s, 32767, EV_CHOKE, 0);
        }
        s->cpk[2] = s->cpk[1]; s->cpk[1] = s->cpk[0]; s->cpk[0] = pk;
    } else { s->cpk[0] = s->cpk[1] = s->cpk[2] = 0; s->crearm = 0; }

    int32_t fast = s->fast, slow = s->slow, env = s->env, dec = s->dec, g = s->g, rearm = s->rearm, depth = s->depth, gmin = s->gmin;
    for (int i = 0; i < OP1_FX_BLOCK; i++) {
        int32_t vel = -1;                                                 /* Q15 velocity of a trigger here */
        if (src == S_BEAT) { if (i == pulse) { vel = pvel; } }
        else {
            int32_t v = x ? x[i] : r ? ((l[i] < 0 ? -l[i] : l[i]) > (r[i] < 0 ? -r[i] : r[i]) ? l[i] : r[i]) : l[i];
            if (v < 0) v = -v;
            v <<= 8;
            fast += (v - fast) >> 7;                                      /* level, ~3 ms */
            if (!(++s->tick & 31)) { s->hist[s->hi] = fast; s->hi = (s->hi + 1) & 7; }
            slow = s->hist[s->hi];                                        /* the level ~6 ms ago */
            s->lng += (fast - s->lng) >> 12;                              /* the level over ~90 ms */
            if (rearm > 0) rearm--;
            else if ((fast >> 8) > FLOOR && fast > slow * 3 && slow < (s->lng >> (hit ? 2 : 1))) {   /* a jump out of a gap */
                rearm = REARM; s->pw = PEAKWIN; s->pv = 0; s->pfire = 0;
            }
            if (s->pw > 0) {                                              /* the hit's strength: its peak level in 10 ms */
                s->pw--;
                int32_t pk = fast >> 8, vv = pk >= 16383 ? 32767 : pk * 2;   /* mean |x| of half scale and up = a full hit */
                if (pk > thr && vv > s->pv) { s->pv = vv; vel = vv; s->pfire = 1; }
                if (s->pw == 0) {                                         /* window over: tell the screen hit or miss */
                    s->hits++;
                    push_ev(s, s->pfire ? s->pv : pk * 2, (s->pfire ? 0 : EV_MISS) | (on_the_one(s, &c) ? EV_DOWN : 0), 0);
                }
            }
        }
        if (vel >= 0) {
            /* PercAll strength: depth and length scale together */
            int32_t st15 = (int32_t)(((int64_t)vel * strength_of(ks)) >> 15);
            int32_t cur = (depth * (env >> 15)) >> 15;                   /* what is left of the last hit */
            if (st15 > cur) {
                depth = st15; env = 1 << 30;
                int32_t n = (int32_t)(((int64_t)dsamp * (8192 + ((st15 * 24575) >> 15))) >> 15);   /* softer = shorter */
                if (n < 64) n = 64;
                dec = (1 << 30) / n;                                      /* the phase falls straight to 0 over n samples */
            }
        }
        if (i == choke_at) env = 0;                                       /* choked: the hit ends here */
        int32_t f = (depth * shape_of(env >> 15, sh)) >> 15;          /* Q15 x Q15 fits 32 bits */
        int32_t gt = (((32767 - wg) * (32767 - f)) >> 15) + ((wg * f) >> 15);   /* flip: duck .. flat .. gate */
        int32_t dg = gt - g;                                              /* attack slews the gain both ways */
        g += dg > ca ? ca : dg < -ca ? -ca : dg;
        if (g < gmin) gmin = g;
        env = env > dec ? env - dec : 0;
        l[i] = op1_sat16((l[i] * g) >> 15);
        if (r) r[i] = op1_sat16((r[i] * g) >> 15);
    }
    s->fast = fast; s->slow = slow; s->env = env; s->dec = dec; s->g = g; s->rearm = rearm; s->depth = depth; s->shown = (depth * (env >> 15)) >> 15; s->gmin = gmin;
}

static void dc_render(int pos, int16_t *buf, const int16_t *p) { process(pos, buf, 0, p); }
static void dc_render_stereo(int pos, int16_t *l, int16_t *r, const int16_t *p) { process(pos, l, r, p); }

const op1_fx_stereo_t FX_duckcall = { { dc_init, dc_render, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_LATCH } },
                                      dc_render_stereo };

#include "duckcall_screen.c"
