#!/usr/bin/env python3
"""Duck Call's screen art: writes duckcall.svg (everything still, drawn once through the static-prefix cache) and
duckcall_geom.h (the moving parts' geometry for duckcall_screen.c). Same layout as the playable prototype, fitted to
the effect screen's safe rows (art above y 146).
    python3 -I gen_screen.py
Copyright (C) 2026 Steph Simorka. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE)."""
import math, os

HERE = os.path.dirname(os.path.abspath(__file__))
IND, GR, GRL = '#383572', '#6F7173', '#A3A5A8'

def f(v):                      # at most 2 decimals, no float noise
    s = ('%.2f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s

def quad(p0, c, p1, n):       # sample a quadratic curve, n points after p0
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * c[0] + t * t * p1[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * c[1] + t * t * p1[1]) for t in [(i + 1) / n for i in range(n)]]

def poly(pts, closed=False):
    d = 'M' + ' L'.join(f(x) + ',' + f(y) for x, y in pts)
    return d + (' Z' if closed else '')

out = []
def el(s): out.append(s)

# ---------- the back wall: sunburst rays, kept to the band above the ducks and between the curtains ----------
CX, CY = 160, 98
rays = []
for i in range(14):
    a = math.pi + i * math.pi / 13
    x0, y0 = CX + math.cos(a) * 30, CY + math.sin(a) * 30
    x1, y1 = CX + math.cos(a) * 170, CY + math.sin(a) * 170
    # clip to 9 <= y <= 52 and 60 <= x <= 260
    pts = []
    for t in [k / 400 for k in range(401)]:
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        pts.append((x, y, 9 <= y <= 52 and 60 <= x <= 260))
    run = [(x, y) for x, y, ok in pts if ok]
    if len(run) > 1:
        rays.append((run[0], run[-1]))
el('<path d="' + ' '.join(f'M{f(a[0])},{f(a[1])} L{f(b[0])},{f(b[1])}' for a, b in rays) + f'" fill="none" stroke="{IND}" stroke-width="1"/>')

# ---------- clouds on strings (filled black so the rays stop at their edge) ----------
def cloud(cx, cy):
    el(f'<line x1="{f(cx)}" y1="9" x2="{f(cx)}" y2="{f(cy - 9)}" stroke="{IND}" stroke-width="1"/>')
    pts = []
    for (ox, oy, r, a0, a1) in [(-9, 1, 6.5, 0.7, 1.75), (1, -3, 8, 1.05, 1.95), (11, 1, 6, 1.25, 2.35)]:
        n = 7
        for k in range(n + 1):
            a = math.pi * (a0 + (a1 - a0) * k / n)
            pts.append((cx + ox + math.cos(a) * r, cy + oy + math.sin(a) * r))
    pts.append((cx - 15, cy + 5))
    q = [pts[0]]
    for pt in pts[1:]:
        if (pt[0] - q[-1][0]) ** 2 + (pt[1] - q[-1][1]) ** 2 > 0.25: q.append(pt)
    def bad(a, b, c):
        v1, v2 = (b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])
        l1, l2 = math.hypot(*v1), math.hypot(*v2)
        return (v1[0] * v2[0] + v1[1] * v2[1]) / (l1 * l2) < -0.9
    changed = True
    while changed:                     # drop the points where the outline turns back (R246 draws a spike there)
        changed = False
        for i in range(1, len(q) - 1):
            if bad(q[i - 1], q[i], q[i + 1]): del q[i]; changed = True; break
    pts = q
    el(f'<path d="{poly(pts, True)}" fill="#000000" stroke="{IND}" stroke-width="1.2" stroke-linejoin="round"/>')
cloud(86, 46)
cloud(236, 42)

# ---------- the marquee: black arch over the top, outline, lettering, rules, unlit bulbs ----------
def cub(p0, c, p2):            # quadratic -> cubic control points
    return ((p0[0] + 2 / 3 * (c[0] - p0[0]), p0[1] + 2 / 3 * (c[1] - p0[1])),
            (p2[0] + 2 / 3 * (c[0] - p2[0]), p2[1] + 2 / 3 * (c[1] - p2[1])))
(l1, l2), (r1, r2) = cub((98, 8), (104, 30), (120, 30)), cub((200, 30), (216, 30), (222, 8))
arch = (f'M0,0 L320,0 L320,8 L222,8 C{f(r2[0])},{f(r2[1])} {f(r1[0])},{f(r1[1])} 200,30 L120,30 '
        f'C{f(l2[0])},{f(l2[1])} {f(l1[0])},{f(l1[1])} 98,8 L0,8 Z')
el(f'<path d="{arch}" fill="#000000"/>')
el(f'<path d="M0,8 L98,8 C{f(l1[0])},{f(l1[1])} {f(l2[0])},{f(l2[1])} 120,30 L200,30 C{f(r1[0])},{f(r1[1])} {f(r2[0])},{f(r2[1])} 222,8 L320,8" '
   f'fill="none" stroke="{GRL}" stroke-width="2.5" stroke-linejoin="round"/>')
# lettering: the kennel stroke font (seq_text.h's 4 x 6 grid), slab weight with an indigo drop shadow
# lettering: the kennel stroke font's letters as connected strokes, one path each (R246 joins the subpaths of one
# stroked path into a single line, which drew spikes between letters), no caps or joins it can't draw
STROKES = {
    'D': [[(0, 0), (3, 0), (4, 1), (4, 5), (3, 6), (0, 6), (0, 0)]],
    'U': [[(0, 0), (0, 6), (4, 6), (4, 0)]],
    'C': [[(4, 0), (0, 0), (0, 6), (4, 6)]],
    'K': [[(0, 0), (0, 6)], [(4, 0), (0, 3), (4, 6)]],
    'A': [[(0, 6), (0, 0), (4, 0), (4, 6)], [(0, 3), (4, 3)]],
    'L': [[(0, 0), (0, 6), (4, 6)]],
}
S = 1.8
title, x = 'DUCK CALL', 0
runs = []
for ch in title:
    for run in STROKES.get(ch, []):
        runs.append([((x + px) * S, py * S) for px, py in run])
    x += 6
w = (x - 2) * S
ox, oy = 160 - w / 2, 11.5
for col, dx in ((IND, 1.3), (GRL, 0)):
    for run in runs:
        d = 'M' + ' L'.join(f'{f(ox + dx + px)},{f(oy + dx + py)}' for px, py in run)
        el(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="1.8"/>')
el(f'<line x1="124" y1="6.5" x2="196" y2="6.5" stroke="{GR}" stroke-width="1"/>')
el(f'<line x1="124" y1="25.5" x2="196" y2="25.5" stroke="{GR}" stroke-width="1"/>')
BULBS = [(122 + i * 8.5, 34) for i in range(10)] + [(18 + i * 20, 12) for i in range(5)] + [(222 + i * 20, 12) for i in range(5)]
for (bx, by) in BULBS:
    el(f'<circle cx="{f(bx)}" cy="{f(by)}" r="1.6" fill="none" stroke="{GR}" stroke-width="1.2"/>')

# ---------- the counter's edge (a gap where the barrel rests) ----------
el(f'<line x1="0" y1="104" x2="140" y2="104" stroke="{GRL}" stroke-width="2.5"/>')
el(f'<line x1="180" y1="104" x2="320" y2="104" stroke="{GRL}" stroke-width="2.5"/>')

# ---------- the knobs' still parts: track, scale, body ----------
KNOBS = [(44, 114, 0), (98, 114, 0), (222, 114, -1), (276, 114, 16)]   # x, y, steps (0 = continuous, -1 = drawn live)
KR = 9
A0, A1 = math.pi * 0.75, math.pi * 2.25
for (kx, ky, steps) in KNOBS:
    pts = [(kx + math.cos(A0 + (A1 - A0) * i / 24) * (KR + 4.5), ky + math.sin(A0 + (A1 - A0) * i / 24) * (KR + 4.5)) for i in range(25)]
    el(f'<path d="{poly(pts)}" fill="none" stroke="{IND}" stroke-width="2"/>')
    if steps >= 0:
        n = 11 if steps == 0 else steps
        r0, r1 = (KR + 7, KR + 9) if steps == 0 else (KR + 6.5, KR + 9)
        d = []
        for i in range(n):
            a = A0 + (A1 - A0) * i / (n - 1)
            d.append(f'M{f(kx + math.cos(a) * r0)},{f(ky + math.sin(a) * r0)} L{f(kx + math.cos(a) * r1)},{f(ky + math.sin(a) * r1)}')
        el(f'<path d="{" ".join(d)}" fill="none" stroke="{GR}" stroke-width="1"/>')
    el(f'<circle cx="{kx}" cy="{ky}" r="{KR}" fill="none" stroke="{GRL}" stroke-width="1.6"/>')

svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="160" viewBox="0 0 320 160">\n'
       '<!-- Duck Call (Steph Simorka, GPL-3.0-or-later with the OP-1 firmware permission): the still art, generated by gen_screen.py -->\n'
       + '\n'.join(out) + '\n</svg>\n')
open(os.path.join(HERE, 'duckcall.svg'), 'w').write(svg)

# ---------- moving parts' geometry, for duckcall_screen.c ----------
def q4(v): return int(round(v * 16))
# the rubber duck in duck units (as the prototype), body loop, head ring, bill, wing, eye
body = [(-12, -9)]
body += quad((-12, -9), (-14, -13), (-11, -15), 2)
body += quad((-11, -15), (-10, -10), (-6, -9), 2)
body += quad((-6, -9), (-1, -10), (1, -11), 2)
body += [(9, -9)]
body += quad((9, -9), (13, -6), (11, -2), 2)
body += quad((11, -2), (8, 1), (0, 1), 2)
body += [(-7, 1)]
body += quad((-7, 1), (-13, 0), (-12, -9), 2)
head = [(4 + 5.5 * math.cos(2 * math.pi * k / 10), -16 + 5.5 * math.sin(2 * math.pi * k / 10)) for k in range(11)]
bill = [(9.2, -17)] + quad((9.2, -17), (14, -17), (15, -15), 2) + quad((15, -15), (14, -13), (9.4, -14), 2)
wing = [(-5, -6)] + quad((-5, -6), (-1, -3), (4, -6), 2)

# curtains: inner edge per shape (pump, linear, hold, choke), as the prototype's edgeX, rows 9 .. 104
TOP, TIE, BOT = 10, 66, 104
def shapef(p, s):
    u = 1 - p
    return [p * p, p, 1 - u * u, 1 - u ** 4][s]
def edge(y, s):
    close = s / 3; wd = 32 + close * 10; tie = 12 + close * 3
    if y <= TIE:
        t = (y - TOP) / (TIE - TOP); return tie + wd * shapef(1 - t, s)
    u = (y - TIE) / (BOT - TIE); p0, p1, p2 = tie, tie + 4, 28 + close * 6
    return (1 - u) ** 2 * p0 + 2 * (1 - u) * u * p1 + u * u * p2
CROWS = [TOP + (TIE - TOP) * i / 7 for i in range(7)] + [TIE + (BOT - TIE) * i / 4 for i in range(5)]

h = ['/* generated by gen_screen.py: the moving parts of Duck Call\'s screen. GPL-3.0-or-later (see duckcall.c). */',
     '#ifndef DUCKCALL_GEOM_H', '#define DUCKCALL_GEOM_H',
     '/* the rubber duck, duck units x 16 (Q4), y up is negative */']
def arr(name, pts):
    h.append(f'static const int16_t {name}[{len(pts)}][2] = {{ ' + ', '.join(f'{{{q4(x)},{q4(y)}}}' for x, y in pts) + ' };')
arr('dk_body', body)
arr('dk_head', head)
arr('dk_bill', bill)
arr('dk_wing', wing)
h.append('/* curtain inner edge per shape at CR_N rows from y 10 to 104: x in Q10; the tie row is index CR_TIE */')
h.append(f'#define CR_N {len(CROWS)}')
h.append('static const int32_t cr_y[CR_N] = { ' + ', '.join(str(int(round(y * 1024))) for y in CROWS) + ' };')
h.append('static const int32_t cr_x[4][CR_N] = {')
for s in range(4):
    h.append('  { ' + ', '.join(str(int(round(edge(y, s) * 1024))) for y in CROWS) + ' },')
h.append('};')
tie_i = min(range(len(CROWS)), key=lambda i: abs(CROWS[i] - TIE))
h.append(f'#define CR_TIE {tie_i}')
h.append('/* curtain edge at the two wave rows (80, 97) per shape, Q10, so the waves stop at the curtains */')
h.append('static const int32_t cr_wave[4][2] = { ' + ', '.join('{%d,%d}' % (int(edge(80, s) * 1024), int(edge(97, s) * 1024)) for s in range(4)) + ' };')
h.append(f'#define N_BULBS {len(BULBS)}')
h.append('static const int16_t bulbs[N_BULBS][2] = { ' + ', '.join(f'{{{q4(x)},{q4(y)}}}' for x, y in BULBS) + ' };')
h.append('#endif')
open(os.path.join(HERE, 'duckcall_geom.h'), 'w').write('\n'.join(h) + '\n')
print('duckcall.svg', len(svg.encode()), 'bytes;', len(rays), 'rays')
