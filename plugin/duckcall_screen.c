/* DUCK CALL's screen: an early 20th-century carnival duck shoot (Steph Simorka's design, ported from the playable
 * prototype). Included at the end of duckcall.c, so it reads the plugin's state (st[pos]) directly.
 *   the ducks:   every hit knocks a duck down (the 1 takes the back pair, other hits take turns along the front row),
 *                for as long as the dip lasts; in gate they stand up on a hit instead; flat leaves them standing.
 *                A miss (a hit under the sensitivity) makes them duck; a hit makes the standing ones wobble.
 *   the waves:   blue, sharp with a short attack, rounded with a long one; they roll only while sound runs.
 *   the curtains: white, their edge is the shape (pump, linear, hold, choke).
 *   the rifle:   aims with the flip (gate left, flat centre, duck right) and fires a red flash on every hit.
 *   the sight:   red, one per source; beat sources light the pulse that fired.
 * The marquee, rays, clouds, counter and the knobs' scales are the SVG (drawn once through the static-prefix cache).
 * Everything here is lines (op1_raster_line); things behind are clipped against what is in front (2 px steps,
 * bisected at the edges), as Sister does.
 * Copyright (C) 2026 Steph Simorka. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include "../../seq/seq_text.h"
#include "duckcall_geom.h"

static const uint8_t C_B[4] = { 0x69, 0x8E, 0xFF, 255 }, C_G[4] = { 0x00, 0xED, 0x95, 255 }, C_W[4] = { 255, 255, 255, 255 },
                     C_R[4] = { 0xFF, 0x3A, 0x5D, 255 }, C_IND[4] = { 0x38, 0x35, 0x72, 255 },
                     C_GR[4] = { 0x6F, 0x71, 0x73, 255 }, C_GRL[4] = { 0xA3, 0xA5, 0xA8, 255 };

#define Q8(v)   ((int32_t)((v) * 256))
#define REACT   60                       /* ms: a duck reacts this long after the shot (the screen only) */
#define YMAX    Q8(158)
static void *R;
static unsigned n_lines;

static inline int32_t SN(uint32_t a) { return OP1_SINE_TABLE[a >> 19]; }                /* Q15, a full turn = 2^32 */
static inline int32_t CS(uint32_t a) { return OP1_SINE_TABLE[(a + 0x40000000u) >> 19]; }
static inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* one line, Q8 px; cut at the bottom row, kept on the screen */
static void ln(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w8, const uint8_t *c)
{
    if (y0 > YMAX && y1 > YMAX) return;
    if (y1 > YMAX) { x1 = x0 + (int32_t)((int64_t)(x1 - x0) * (YMAX - y0) / (y1 - y0)); y1 = YMAX; }
    if (y0 > YMAX) { x0 = x1 + (int32_t)((int64_t)(x0 - x1) * (YMAX - y1) / (y0 - y1)); y0 = YMAX; }
    if ((x0 < 0 && x1 < 0) || (x0 > Q8(320) && x1 > Q8(320)) || (y0 < 0 && y1 < 0)) return;
    x0 = x0 < 0 ? 0 : x0 > Q8(320) ? Q8(320) : x0; x1 = x1 < 0 ? 0 : x1 > Q8(320) ? Q8(320) : x1;
    y0 = y0 < 0 ? 0 : y0; y1 = y1 < 0 ? 0 : y1;
    op1_pt a = { x0 * 4, y0 * 4 }, b = { x1 * 4, y1 * 4 };
    op1_raster_line(R, &a, &b, w8 * 4, c);
    n_lines++;
}

/* ---------------- state ---------------- */
static int started;
static unsigned spos;
static uint32_t now, seen, last_sound, run_t;     /* ms */
static int32_t hitT[6], hitD[6], sink[6];
static int32_t wob_t0, wob_a, dodge_t, bulb_t, flash_t, muzzle_t, muzzle_big, choke_t;
static unsigned turn, chase;
static int32_t aim, kv[4];
static uint32_t wph;
static int32_t wk, woff[2];
static struct { int32_t t0, s; uint8_t id; } stars[8];

static void clear_scene(void)
{
    for (int i = 0; i < 6; i++) { hitT[i] = -100000; hitD[i] = 0; }
    wob_t0 = dodge_t = bulb_t = flash_t = muzzle_t = choke_t = -100000;
    for (int i = 0; i < 8; i++) stars[i].t0 = -100000;
}
static void scr_init(void *scr) { (void)scr; started = 0; }

/* ---------------- waves ---------------- */
static const int32_t W_Y[2] = { Q8(80), Q8(97) }, W_A[2] = { 9, 11 }, W_K[2] = { 262144, 209715 };   /* 2^31 / (period x 256) */
static int32_t wave_y(int w, int32_t x8)
{
    const uint32_t a = (uint32_t)(x8 + woff[w]) * (uint32_t)W_K[w];        /* (x + off) / period x pi, wrapping */
    const int32_t s = SN(a), c = iabs(CS(a));
    const int32_t f = ((wk * (32767 - c)) >> 15) + (((32767 - wk) * ((s * s) >> 15)) >> 15);   /* sharp .. smooth */
    return W_Y[w] - f * W_A[w] / 128;
}

/* ---------------- ducks ---------------- */
typedef struct { int32_t x, y, s, cs, sn; } xf_t;                         /* anchor Q8, scale Q8, rotation Q15 */
static xf_t dxf[6];
static const int32_t D_X[6] = { Q8(106), Q8(214), Q8(72), Q8(124), Q8(196), Q8(250) };
static const int32_t D_B[6] = { Q8(74), Q8(74), Q8(92), Q8(92), Q8(92), Q8(92) };
static const int32_t D_S[6] = { 184, 184, 243, 243, 243, 243 };            /* 0.72, 0.95 */

static void xf_pt(const xf_t *t, int32_t u, int32_t v, int32_t *X, int32_t *Y)   /* duck units Q4 -> screen Q8 */
{
    const int32_t ru = (u * t->cs - v * t->sn) >> 15, rv = (u * t->sn + v * t->cs) >> 15;
    *X = t->x + ((ru * t->s) >> 4);
    *Y = t->y + ((rv * t->s) >> 4);
}
static int in_poly(const int16_t (*p)[2], int n, int32_t u, int32_t v)
{
    int c = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const int32_t yi = p[i][1], yj = p[j][1];
        if ((yi > v) != (yj > v) && u < p[j][0] + (p[i][0] - p[j][0]) * (v - yj) / (yi - yj)) c ^= 1;
    }
    return c;
}
static int duck_hides(const xf_t *t, int32_t X, int32_t Y)              /* is (X, Y) behind duck t's black body? */
{
    const int32_t dx = X - t->x, dy = Y - t->y;
    if (dx < -16 * t->s || dx > 18 * t->s || dy > 4 * t->s || dy < -24 * t->s) return 0;     /* 1 duck unit = s (Q8 px) */
    const int32_t u = ((dx * t->cs + dy * t->sn) >> 15) * 16 / t->s, v = ((-dx * t->sn + dy * t->cs) >> 15) * 16 / t->s;
    const int32_t hu = u - 64, hv = v + 256;
    if (hu * hu + hv * hv < 88 * 88) return 1;
    return in_poly(dk_body, sizeof dk_body / sizeof dk_body[0], u, v) || in_poly(dk_bill, sizeof dk_bill / sizeof dk_bill[0], u, v);
}

/* ---------------- clipping: what may this stroke show through? ---------------- */
enum { CK_NONE, CK_BACK, CK_FAR, CK_FRONT };
static int clip_kind;
static int vis(int32_t x, int32_t y)
{
    switch (clip_kind) {
    case CK_BACK:
        if (y >= wave_y(0, x) || y >= wave_y(1, x)) return 0;
        for (int i = 2; i < 6; i++) if (duck_hides(&dxf[i], x, y)) return 0;
        return 1;
    case CK_FAR:
        for (int i = 2; i < 6; i++) if (duck_hides(&dxf[i], x, y)) return 0;
        return 1;
    case CK_FRONT: return y < wave_y(1, x);
    default: return 1;
    }
}
static void cline(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w8, const uint8_t *c)
{
    if (clip_kind == CK_NONE) { ln(x0, y0, x1, y1, w8, c); return; }
    const int32_t dx = x1 - x0, dy = y1 - y0, L = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
    int n = L / Q8(2) + 1;
    if (n > 24) n = 24;
    int32_t sx = x0, sy = y0, px = x0, py = y0;
    int pv = vis(x0, y0);
    for (int i = 1; i <= n; i++) {
        const int32_t qx = x0 + dx * i / n, qy = y0 + dy * i / n;
        const int qv = vis(qx, qy);
        if (qv != pv) {
            int32_t ax = px, ay = py, bx = qx, by = qy;
            for (int k = 0; k < 3; k++) {
                const int32_t mx = (ax + bx) / 2, my = (ay + by) / 2;
                if (vis(mx, my) == pv) { ax = mx; ay = my; } else { bx = mx; by = my; }
            }
            if (pv) ln(sx, sy, ax, ay, w8, c); else { sx = bx; sy = by; }
        }
        px = qx; py = qy; pv = qv;
    }
    if (pv) ln(sx, sy, x1, y1, w8, c);
}
static void dpoly(const xf_t *t, const int16_t (*p)[2], int n, int32_t w8, const uint8_t *c)
{
    int32_t px, py;
    xf_pt(t, p[0][0], p[0][1], &px, &py);
    for (int i = 1; i < n; i++) {
        int32_t x, y;
        xf_pt(t, p[i][0], p[i][1], &x, &y);
        cline(px, py, x, y, w8, c);
        px = x; py = y;
    }
}
static void draw_duck(int id, const uint8_t *col)
{
    const xf_t *t = &dxf[id];
    int32_t ax, ay, bx, by;
    xf_pt(t, 0, 16, &ax, &ay); xf_pt(t, 0, 480, &bx, &by);
    cline(ax, ay, bx, by, t->s * 3 / 2, C_GR);                                  /* the stick */
    const int32_t w = t->s * 9 / 5;
    dpoly(t, dk_body, sizeof dk_body / sizeof dk_body[0], w, col);
    dpoly(t, dk_bill, sizeof dk_bill / sizeof dk_bill[0], w, col);
    dpoly(t, dk_head, sizeof dk_head / sizeof dk_head[0], w, col);
    dpoly(t, dk_wing, sizeof dk_wing / sizeof dk_wing[0], w, col);
    xf_pt(t, 88, -280, &ax, &ay);                                               /* the eye */
    cline(ax - t->s / 2, ay, ax + t->s / 2, ay, t->s * 2, col);
}

/* ---------------- small shapes ---------------- */
static void circ(int32_t cx, int32_t cy, int32_t r8, int n, int32_t w8, const uint8_t *c)
{
    int32_t px = cx + r8, py = cy;
    for (int i = 1; i <= n; i++) {
        const uint32_t a = (uint32_t)((0x100000000ull * i) / n);
        const int32_t x = cx + ((r8 * CS(a)) >> 15), y = cy + ((r8 * SN(a)) >> 15);
        ln(px, py, x, y, w8, c);
        px = x; py = y;
    }
}
static void ray(int32_t cx, int32_t cy, uint32_t a, int32_t r0, int32_t r1, int32_t w8, const uint8_t *c)
{
    const int32_t s = SN(a), k = CS(a);
    ln(cx + ((r0 * k) >> 15), cy + ((r0 * s) >> 15), cx + ((r1 * k) >> 15), cy + ((r1 * s) >> 15), w8, c);
}
static void arc(int32_t cx, int32_t cy, int32_t r8, uint32_t a0, uint32_t span, int32_t w8, const uint8_t *c)
{
    int n = (int)(span >> 28) + 1;                                              /* a segment per 1/16 turn */
    int32_t px = cx + ((r8 * CS(a0)) >> 15), py = cy + ((r8 * SN(a0)) >> 15);
    for (int i = 1; i <= n; i++) {
        const uint32_t a = a0 + (uint32_t)(((uint64_t)span * i) / n);
        const int32_t x = cx + ((r8 * CS(a)) >> 15), y = cy + ((r8 * SN(a)) >> 15);
        ln(px, py, x, y, w8, c);
        px = x; py = y;
    }
}
static int32_t text_w(const char *t, int32_t s)                              /* Q10 */
{
    int32_t w = 0;
    for (; *t; t++) w += (*t == '1' || *t == '.' ? 4 : 6) * s;
    return w ? w - 2 * s : 0;
}
static void ctext(int32_t cx8, int32_t y8, const char *t, const uint8_t *c)   /* centred, 1.25 px a grid unit, narrower to fit 44 px */
{
    int32_t s = 1280;
    const int32_t w = text_w(t, 1024);
    if (w * 5 / 4 > 44 * 1024) s = (int32_t)(44LL * 1024 * 1024 / w);
    if (s < 820) s = 820;
    sq_text(R, cx8 * 4 - text_w(t, s) / 2, y8 * 4, s, t, 1200, c);
}

/* ---------------- maths shared with the DSP ---------------- */
static int32_t log2_q8(uint32_t v)                                           /* log2(v / 256), Q8, v >= 256 */
{
    if (v <= 256) return 0;
    int o = 0;
    while ((v >> (o + 1)) > 0) o++;
    return ((o - 8) << 8) + (int32_t)(((v << 8) >> o) - 256);
}
static int32_t exp_q15(int32_t x)                                             /* exp(-x / 1000) for x >= 0, as (1 - y/8)^8 */
{
    int32_t q = 32767 - x * 4;                                                /* 32767 / 8000 = 4.1 */
    if (q <= 0) return 0;
    q = (q * q) >> 15; q = (q * q) >> 15; return (q * q) >> 15;
}
static char *num(char *p, int32_t v) { return sq_num(p, (uint32_t)(v < 0 ? 0 : v), 1); }

static const int16_t RIF[] = {      /* the rifle, half pixels from (160, 96): count, colour (0 = RC, 1 = GR), width x 10, x, y ... */
    4, 0, 22, -36, 132, -8, 8, 8, 8, 36, 132,
    2, 1, 12, 0, 132, 0, 12,
    4, 0, 20, -3, 8, -2, -4, 2, -4, 3, 8,
    8, 0, 20, -20, 60, -20, 48, -8, 48, -4, 56, 4, 56, 8, 48, 20, 48, 20, 60,
    3, 0, 22, -72, 132, -52, 68, -38, 64,
    3, 0, 22, 72, 132, 52, 68, 38, 64,
    2, 1, 10, -58, 132, -46, 74,
    2, 1, 10, 58, 132, 46, 74,
    0 };

static void scr_update(void *scr, int dt)
{
    if (dt <= 0 || dt > 200) dt = 33;
    const unsigned pos = (unsigned)op1_fx_screen_pos() % 3;
    dc_t *s = &st[pos];
    int16_t kn[4];
    op1_fx_knobs_live(kn);
    const int16_t *pw = op1_fx_knobs(pos);
    int32_t k[4], w2[3];
    for (int i = 0; i < 4; i++) k[i] = kn[i] < 0 ? 0 : kn[i];
    for (int i = 0; i < 3; i++) w2[i] = pw[5 + i] < 0 ? 0 : pw[5 + i];
    const int page[4] = { op1_fx_page[pos][0] != 0, op1_fx_page[pos][1] != 0, op1_fx_page[pos][2] != 0, op1_fx_page[pos][3] != 0 };
    const int ent = ent_of(k[3]), beat = ent < NDIV;

    if (!started || spos != pos) { started = 1; spos = pos; seen = s->evseq; last_sound = s->sounding; now = 0; run_t = -100000; run_t = 0;
                                   clear_scene(); for (int i = 0; i < 6; i++) sink[i] = 0; aim = w2[2] * 2 - 32767;
                                   for (int i = 0; i < 4; i++) kv[i] = 0; turn = chase = 0; }
    now += (uint32_t)dt;
    if (s->sounding != last_sound) { last_sound = s->sounding; run_t = now; }
    const int playing = (int32_t)(now - run_t) < 400;              /* sound coming through or the tape running */

    /* the knobs as the DSP reads them */
    const int32_t dsamp = 661 + (int32_t)(((int64_t)((k[0] * k[0]) >> 15) * 87539) >> 15);
    const int32_t len_ms = dsamp * 10 / 441;
    const int32_t asamp = 9 + (((w2[0] * w2[0]) >> 15) * 4401 >> 15);
    const int32_t strf = strength_of(k[1]);                      /* strength factor, Q15 */
    const int32_t flipq = w2[2] * 2 - 32767;                                  /* -1 gate .. +1 duck, Q15 */
    const int mode = w2[2] < 14745 ? 1 : w2[2] > 18022 ? 0 : 2;               /* 0 duck, 1 gate, 2 flat */
    const int si = (k[2] * 3 + 16383) >> 15;                                  /* the nearest shape */

    /* the hit feed */
    if (s->evseq - seen > 8) seen = s->evseq - 8;
    while (seen != s->evseq) {
        seen++;
        const dc_ev_t e = s->ev[seen & 7];
        if (!playing) continue;
        if (e.flags & EV_MISS) { dodge_t = (int32_t)now; continue; }
        if (e.flags & EV_CHOKE) { choke_t = (int32_t)now; for (int i = 0; i < 6; i++) hitT[i] = -100000; continue; }   /* every dip ends */
        const int down = (e.flags & EV_DOWN) != 0;
        const int32_t d = (e.vel * strf) >> 15;
        wob_t0 = (int32_t)now; wob_a = 2621 + ((e.vel * 7209) >> 15);
        muzzle_t = (int32_t)now; muzzle_big = down; flash_t = (int32_t)now; chase++;
        if (down) bulb_t = (int32_t)now;
        int ids[2], n = 0;
        if (down) { ids[0] = 0; ids[1] = 1; n = 2; } else { ids[0] = 2 + (int)(turn++ & 3); n = 1; }
        for (int j = 0; j < n; j++) {
            hitT[ids[j]] = (int32_t)now; hitD[ids[j]] = d;
            unsigned m = 0;
            for (unsigned q = 1; q < 8; q++) if (stars[q].t0 < stars[m].t0) m = q;
            stars[m].t0 = (int32_t)now + REACT; stars[m].s = d; stars[m].id = (uint8_t)ids[j];
        }
    }
    if (!playing) clear_scene();
    const int32_t T = (int32_t)now;

    /* the waves: roll while sound runs, faster with a shorter length; sharp with a short attack */
    if (playing) wph += (uint32_t)(512673956 / (len_ms < 60 ? 60 : len_ms)) * (uint32_t)dt;
    {
        int32_t lg = log2_q8((uint32_t)asamp * 256 / 9);
        wk = 32767 - (lg >= 2288 ? 32767 : lg * 32767 / 2288);
        const int32_t sw = (5 * 256 * SN(wph)) >> 15;
        woff[0] = sw; woff[1] = -sw + Q8(16);
    }
    for (int i = 0; i < 4; i++) {
        const int32_t t = i == 0 ? (page[0] ? w2[0] : k[0]) : i == 1 ? (page[1] ? w2[1] : k[1]) : i == 2 ? (page[2] ? w2[2] : k[2])
                        : (ent * 32767 / (NENT - 1));
        kv[i] += ((t - kv[i]) * 9830) >> 15;
        if (iabs(t - kv[i]) < 64) kv[i] = t;
    }
    aim += ((flipq - aim) * 6554) >> 15;

    /* the ducks' poses */
    const int32_t wt = T - wob_t0, wr = wt >= 0 && wt < 350 ? (int32_t)(((int64_t)wob_a * SN((uint32_t)wt * 25975u) >> 15) * exp_q15(wt * 9) >> 15) : 0;
    const int32_t d2 = T - dodge_t, dr = d2 >= 0 && d2 < 250 ? SN((uint32_t)d2 * 8589934u) : 0;
    const int32_t h2 = T - bulb_t, hop = h2 >= 0 && h2 < 120 ? (3 * 256 * SN((uint32_t)h2 * 17895697u)) >> 15 : 0;
    for (int id = 0; id < 6; id++) {
        int32_t env = 0;
        const int32_t te = T - REACT - hitT[id], dd = hitD[id];
        const int32_t dur = (int32_t)(((int64_t)len_ms * (8192 + ((dd * 24575) >> 15))) >> 15);
        if (te >= 0 && te <= dur && dur > 0) env = (dd * shape_of(32767 - te * 32767 / dur, si * 32767 / 3)) >> 15;
        const int on = env > 6554, down = playing && (mode == 0 ? on : mode == 1 ? !on : 0);
        const int32_t fast = 70, slow = 180, dn = mode == 1 ? slow : fast, up = mode == 1 ? fast : slow;
        const int32_t fdt = dt > 50 ? 50 : dt;
        if (down) { sink[id] += 32767 * fdt / dn; if (sink[id] > 32767) sink[id] = 32767; }
        else { sink[id] -= 32767 * fdt / up; if (sink[id] < 0) sink[id] = 0; }
        const int32_t kq = sink[id], stand = 32767 - kq;
        const int32_t rot = ((((id & 1) ? wr : -wr) - ((dr * 9830) >> 15)) * stand >> 15) - ((((kq * (32767 - kq)) >> 15) * 29491) >> 15);
        const int32_t lift = (((hop - ((dr * 768) >> 15)) * stand) >> 15) - ((kq * (18 * D_S[id])) >> 15);
        const uint32_t a = (uint32_t)(rot * 20861);
        dxf[id].x = D_X[id]; dxf[id].y = D_B[id] - lift; dxf[id].s = D_S[id]; dxf[id].cs = CS(a); dxf[id].sn = SN(a);
    }

    op1_screen_draw_cached(scr, dt, 0);                  /* rays, clouds, marquee, counter, knob scales: all still */
    R = op1_raster();
    n_lines = 0;

    /* back ducks, the far wave, front ducks, the near wave */
    const int32_t wl0 = cr_wave[si][0] >> 2, wl1 = cr_wave[si][1] >> 2;
    clip_kind = CK_BACK;
    for (int id = 0; id < 2; id++) draw_duck(id, sink[id] > 16384 ? C_IND : C_G);
    for (int w = 0; w < 2; w++) {
        clip_kind = w ? CK_NONE : CK_FAR;
        const int32_t x0 = w ? wl1 : wl0, x1 = Q8(320) - x0;
        int32_t px = x0, py = wave_y(w, x0);
        for (int32_t x = x0; x < x1;) {
            x += Q8(5);
            if (x > x1) x = x1;
            const int32_t y = wave_y(w, x);
            cline(px, py, x, y, w ? 512 : 384, C_B);
            px = x; py = y;
        }
        if (!w) { clip_kind = CK_FRONT; for (int id = 2; id < 6; id++) draw_duck(id, sink[id] > 16384 ? C_IND : C_G); }
    }
    clip_kind = CK_NONE;

    /* ricochet stars, in front of the waves */
    if (mode != 2) for (unsigned q = 0; q < 8; q++) {
        const int32_t a = T - stars[q].t0;
        if (a < 0 || a >= 220) continue;
        const int id = stars[q].id, sc = D_S[id];
        const int32_t r8 = ((3 * 256 + (((4 * 256 + 5 * stars[q].s / 128) * a) / 220)) * sc) >> 8;
        const int32_t cx = D_X[id] + 4 * sc, cy = D_B[id] - 17 * sc;
        for (int j = 0; j < 4; j++) ray(cx, cy, 0x40000000u * (uint32_t)j + 273383163u, r8 * 45 / 100, r8, 384, C_G);
    }

    /* the rifle's aim and recoil, the counter beside it */
    const int32_t sx = (aim * 9 * 256) >> 15;                                 /* the sight moves at most 9 px */
    const int mz = T - muzzle_t >= 0 && T - muzzle_t < (muzzle_big ? 140 : 80);
    const int32_t rec = mz ? (muzzle_big ? Q8(2) : Q8(1)) : 0;
    {
        const int32_t skew = sx * 58 / 102;
        ln(Q8(140), Q8(104), Q8(153) + skew, Q8(104), 640, C_GRL);
        ln(Q8(167) + skew, Q8(104), Q8(180), Q8(104), 640, C_GRL);
    }

    /* the curtains: the edge is the shape */
    for (int side = 0; side < 2; side++) {
        const int32_t *ex = cr_x[si];
#define MX(x) (side ? Q8(320) - (x) : (x))
        for (int i = 0; i + 1 < CR_N; i++)
            ln(MX(ex[i] >> 2), cr_y[i] >> 2, MX(ex[i + 1] >> 2), cr_y[i + 1] >> 2, 512, page[2] ? C_GRL : C_W);
        for (int f = 0; f < 2; f++) {
            const int32_t fk = f ? 179 : 90;                                  /* 0.7, 0.35 of the edge (Q8) */
            int32_t px = 0, py = 0;
            for (int i = 1; i < CR_N; i++) {
                const int32_t x = ((ex[i] >> 2) * fk >> 8) * (i == CR_TIE ? 205 : 256) >> 8, y = cr_y[i] >> 2;
                if (i > 1) ln(MX(px), py, MX(x), y, 307, C_IND);
                px = x; py = y;
            }
        }
        const int32_t tx = ex[CR_TIE] >> 2, ty = cr_y[CR_TIE] >> 2;
        int32_t px = 0, py = ty;
        for (int j = 1; j <= 4; j++) {                                        /* the tieback: a quadratic */
            const int32_t u = j * 64, v = 256 - u;
            const int32_t x = (2 * v * u * (tx * 3 / 5) + u * u * (tx + Q8(2))) >> 16;
            const int32_t y = (v * v * ty + 2 * v * u * (ty + Q8(4)) + u * u * (ty - Q8(1))) >> 16;
            ln(MX(px), py, MX(x), y, 512, C_GRL);
            px = x; py = y;
        }
#undef MX
    }

    /* the marquee bulbs: a chase on every hit, all on the 1 */
    if (playing) {
        const int all = T - bulb_t >= 0 && T - bulb_t < 120;
        for (unsigned i = 0; i < N_BULBS; i++)
            if (all || i % 3 == chase % 3) ln((bulbs[i][0] << 4) - Q8(1.6), bulbs[i][1] << 4, (bulbs[i][0] << 4) + Q8(1.6), bulbs[i][1] << 4, Q8(3.2), C_GRL);
    }

    /* the rifle (skewed with the aim) and its flash */
    {
        const uint8_t *rc = page[2] ? C_W : C_GRL;
        const int16_t *p = RIF;
        while (*p) {
            const int n = *p++;
            const uint8_t *c = *p++ ? C_GR : rc;
            const int32_t w = (int32_t)*p++ * 256 / 10;
            int32_t px = 0, py = 0;
            for (int i = 0; i < n; i++, p += 2) {
                const int32_t y = Q8(96) + p[1] * 128;
                const int32_t x = Q8(160) + p[0] * 128 + (int32_t)((int64_t)sx * (Q8(162) - y) / Q8(102));
                if (i) ln(px, py + rec, x, y + rec, w, c);
                px = x; py = y;
            }
        }
        if (mz) {
            const int32_t tx = Q8(160) + sx * 68 / 102, ty = Q8(93), r0 = muzzle_big ? Q8(9) : Q8(5);
            for (int q = 0; q < 7; q++)
                ray(tx, ty, 0xC0000000u + (uint32_t)((q - 3) * 286870681), Q8(2), q & 1 ? r0 * 55 / 100 : r0, 410, C_R);
        }
    }

    /* the sight: one per source */
    {
        const int32_t cx = Q8(160) + sx, cy = Q8(60);
        const uint8_t *col = (!beat && T - flash_t >= 0 && T - flash_t < 90) ? C_W : C_R;
        if (beat) {
            const int div = div_bar[ent], per = div % 4 == 0 ? div / 4 : div / 2, lit = s->lit;
            circ(cx, cy, Q8(10), 16, 512, col);
            for (int q = 0; q < div; q++) {
                const int major = div <= 4 || q % per == 0;
                ray(cx, cy, 0xC0000000u + (uint32_t)((0x100000000ull * q) / div), Q8(10), major ? Q8(15) : Q8(13),
                    div >= 24 ? 358 : 512, playing && q == lit ? C_W : col);
            }
            ln(cx - Q8(1.2), cy, cx + Q8(1.2), cy, Q8(2.4), col);             /* the centre dot */
        } else if (ent >= 8 && ent <= 11) {
            const int t = ent - 7;
            circ(cx, cy, Q8(9), 16, 512, col);
            ln(cx - Q8(13), cy, cx - Q8(4), cy, 512, col); ln(cx + Q8(4), cy, cx + Q8(13), cy, 512, col);
            ln(cx, cy - Q8(13), cx, cy - Q8(4), 512, col); ln(cx, cy + Q8(4), cx, cy + Q8(13), 512, col);
            static const uint32_t diag[4] = { 0xE0000000u, 0x20000000u, 0x60000000u, 0xA0000000u };
            for (int q = 0; q < t; q++) ray(cx, cy, diag[q], Q8(9), Q8(14), 512, col);
            ln(cx - Q8(1.2), cy, cx + Q8(1.2), cy, Q8(2.4), col);
        } else if (ent == 12) {
            circ(cx, cy, Q8(10), 16, 512, col);
            for (int q = 0; q < 3; q++) ray(cx, cy, 0xC0000000u + (uint32_t)q * 1431655765u, Q8(3), Q8(10), 512, col);
            circ(cx, cy, Q8(3), 8, 512, col);
        } else if (ent == 13) {
            ln(cx - Q8(14), cy + Q8(4), cx - Q8(5), cy + Q8(4), 512, col); ln(cx - Q8(5), cy + Q8(4), cx, cy - Q8(5), 512, col);
            ln(cx, cy - Q8(5), cx + Q8(5), cy + Q8(4), 512, col); ln(cx + Q8(5), cy + Q8(4), cx + Q8(14), cy + Q8(4), 512, col);
            ln(cx, cy + Q8(8), cx, cy + Q8(10), 512, col);
        } else {
            circ(cx, cy, Q8(3), 8, 512, col);
            ln(cx, cy - Q8(12), cx, cy - Q8(6), 512, col);
            ln(cx - Q8(6), cy - Q8(13), cx + Q8(6), cy - Q8(13), 512, col);
            if (ent == 15) { ln(cx - Q8(6), cy - Q8(13), cx - Q8(6), cy - Q8(10), 512, col); ln(cx + Q8(6), cy - Q8(13), cx + Q8(6), cy - Q8(10), 512, col); }
        }
    }

    if (T - choke_t >= 0 && T - choke_t < 140) {                              /* a choke: the sight is crossed out */
        const int32_t cx = Q8(160) + sx, cy = Q8(60);
        ln(cx - Q8(14), cy - Q8(14), cx + Q8(14), cy + Q8(14), Q8(2.4), C_R);
        ln(cx - Q8(14), cy + Q8(14), cx + Q8(14), cy - Q8(14), Q8(2.4), C_R);
    }

    /* the knobs: fill, pointer, lit step, value, the page-2 badge */
    {
        static const int32_t KX[4] = { Q8(44), Q8(98), Q8(222), Q8(276) };
        static const uint8_t *const KC[4] = { C_B, C_G, C_W, C_R };
        static const char *const SHN[4] = { "PUMP", "LINEAR", "HOLD", "CHOKE" };
        static const char *const SRCN[NENT] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/16T",
                                                "T1", "T2", "T3", "T4", "TAPE", "IN", "SELF", "INST" };
        const int32_t ky = Q8(114);
        const uint32_t A0 = 0x60000000u, SPAN = 0xC0000000u;
        for (int i = 0; i < 4; i++) {
            const int32_t x = KX[i], f = kv[i];
            const uint8_t *c = KC[i];
            const uint32_t a = A0 + (uint32_t)(((uint64_t)SPAN * (uint32_t)f) >> 15);
            if (i == 2 && page[2]) {                                          /* flip: bipolar from the top */
                const uint32_t off = (uint32_t)(((uint64_t)SPAN * (uint32_t)f) >> 15), mid = SPAN / 2;   /* offsets: no wrap */
                if (off > mid) arc(x, ky, Q8(13.5), A0 + mid, off - mid, 512, c);
                else if (off < mid) arc(x, ky, Q8(13.5), A0 + off, mid - off, 512, c);
                ln(x, ky - Q8(16), x, ky - Q8(20), 256, C_GR);
            } else if (i == 2) {                                              /* shape: four steps */
                for (int q = 0; q < 4; q++) {
                    const uint32_t aq = A0 + (uint32_t)(SPAN / 3 * (uint32_t)q);
                    if (q == si) ray(x, ky, aq, Q8(15.5), Q8(20), 512, c); else ray(x, ky, aq, Q8(15.5), Q8(18), 256, C_GR);
                }
            } else if (i == 3) {                                              /* source: the lit step */
                ray(x, ky, A0 + (uint32_t)((uint64_t)SPAN * (uint32_t)ent / (NENT - 1)), Q8(15.5), Q8(20), 512, c);
            } else if (f > 0) arc(x, ky, Q8(13.5), A0, a - A0, 512, c);
            ray(x, ky, a, Q8(3), Q8(8), 563, c);
            const int p2 = page[i];                                          /* page 2 (red: choke on) */
#ifndef PAGE_STYLE
#define PAGE_STYLE 3                                          /* page 2: inner ring + value chip */
#endif
            if (p2 && (PAGE_STYLE & 1)) circ(x, ky, Q8(6), 12, 384, c);                 /* page 2: a ring inside the knob */
            char b[16], *p = b;
            b[0] = 0;
            if (i == 0) {
                if (page[0]) {                                                /* attack: "ATK 2.6" (ms), "ATK 25" from 10 ms */
                    const int32_t t = asamp * 100 / 441;
                    p = sq_cat(p, "ATK "); p = num(p, t / 10);
                    if (t < 100) { p = sq_cat(p, "."); num(p, t % 10); }
                }
                else { p = num(p, len_ms); sq_cat(p, "MS"); }
            } else if (i == 1) {
                if (page[1]) { p = sq_cat(p, beat ? "ACC " : "SEN "); p = num(p, (w2[1] * 100 + 16383) / 32767); sq_cat(p, "%"); }
                else { p = num(p, (strf * 100 + 16383) >> 15); sq_cat(p, "%"); }
            } else if (i == 2) {
                if (page[2]) {
                    const int32_t pc = (iabs(flipq) * 100 + 16383) / 32767;
                    if (pc >= 99) sq_cat(p, flipq > 0 ? "DUCK" : "GATE");
                    else if (pc < 2) sq_cat(p, "FLAT");
                    else { p = sq_cat(p, flipq > 0 ? "DUCK " : "GATE "); p = num(p, pc); sq_cat(p, "%"); }
                } else sq_cat(p, SHN[si]);
            } else if (page[3]) { p = sq_cat(p, SRCN[ent]); p = sq_cat(p, " CUT "); sq_cat(p, SRCN[choke_of(ent)]); }
            else sq_cat(p, SRCN[ent]);
            if (p2 && (PAGE_STYLE & 2)) {                                     /* page 2: the value on a chip of the knob's colour */
                int32_t sc = 1280; const int32_t tw = text_w(b, 1024);
                if (tw * 5 / 4 > 44 * 1024) sc = (int32_t)(44LL * 1024 * 1024 / tw);
                if (sc < 820) sc = 820;
                const int32_t hw = text_w(b, sc) / 8 + Q8(2.5);                 /* Q8 half width */
                ln(x - hw, ky + Q8(25.75), x + hw, ky + Q8(25.75), Q8(10), c);
                static const uint8_t black[4] = { 1, 1, 1, 255 };
                ctext(x, ky + Q8(22), b, black);
            } else ctext(x, ky + Q8(22), b, c);
        }
    }
}
const op1_screen_t SCREEN_duckcall = { scr_init, scr_update };
