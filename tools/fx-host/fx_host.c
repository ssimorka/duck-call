/* fx_host: run a kennel FX plugin on your computer and listen to it.
 *
 *   cc -O2 -Wall -fsanitize=address,undefined -I. -I<kennel>/synth_sdk \
 *      -DPLUGIN_C='"<kennel>/synth_sdk/plugins/myfx/myfx.c"' -DFX=FX_myfx fx_host.c -lm -o fx_host
 *   ./fx_host 8000 16000 4000 32767                    # the four knobs, 0..32767 (blue green white red)
 *   ./fx_host 8000 16000 4000 32767 11000 16384 32767  # plus the page-2 words for blue green white
 *
 * It plays 8 s of a chord pad over a kick at 120 BPM through your effect and writes dry.wav and wet.wav.
 * The kick is also what op1_sc_source() returns for every sidechain source, and the beat clock runs at 120 BPM.
 * Add -DSTEREO if your effect is stereo (plugin.json "stereo": true) to call render_stereo.
 * Part of Duck Call's fx-host kit, GPL-3.0-or-later (see the repo's LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define OP1_HOST 1
#include "op1_plugin.h"

/* the firmware-side pieces a plugin may import */
static inline int32_t op1_fx_param_q15(const int16_t *p, int i) { return p[i] < 0 ? 0 : p[i]; }  /* host: words are 0..32767 */
volatile uint8_t op1_fx_page[3][4];
volatile uint8_t op1_fx_wrap[3][4] __attribute__((weak));
static int16_t *sc; static long sc_pos;
int op1_sc_source(int src, int16_t *out) { (void)src; memcpy(out, sc + sc_pos, OP1_FX_BLOCK * 2); return 1; }

#include PLUGIN_C

#define SR 44100
#define BPM 120
#define N (SR * 8)
static void wav(const char *fn, const int16_t *l, const int16_t *r, long n)
{
    FILE *f = fopen(fn, "wb"); if (!f) { perror(fn); exit(1); }
    uint32_t sz = 36 + n * 4, rate = SR, br = SR * 4, fs = 16, ds = n * 4; uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fs, 4, 1, f); fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (long i = 0; i < n; i++) { fwrite(&l[i], 2, 1, f); fwrite(&r[i], 2, 1, f); }
    fclose(f);
}
int main(int argc, char **argv)
{
    for (int i = 0; i < 8192; i++) host_sine[i] = (int16_t)lround(sin(2 * M_PI * i / 8192) * 32767);
    int16_t *k = host_fx_knobs[2];
    for (int i = 0; i < 4; i++) k[i] = argc > 1 + i ? (int16_t)atoi(argv[1 + i]) : 16384;
    for (int i = 0; i < 3; i++) k[5 + i] = argc > 5 + i ? (int16_t)atoi(argv[5 + i]) : 16384;   /* page-2 words */

    /* the test signal: Am7 / Fmaj7 detuned saws (left a touch apart from right), kick on every beat */
    int16_t *dl = malloc(N * 2), *dr = malloc(N * 2), *wl = malloc(N * 2), *wr = malloc(N * 2); sc = calloc(N + 64, 2);
    double ph[8] = {0}, lp = 0, lp2 = 0; long beat = SR * 60 / BPM;
    const double c1[4] = {220, 261.63, 329.63, 392}, c2[4] = {174.61, 220, 261.63, 329.63};
    for (long i = 0; i < N; i++) {
        const double *c = (i / (beat * 4)) % 2 ? c2 : c1; double s = 0, s2 = 0;
        for (int j = 0; j < 4; j++) for (int d = 0; d < 2; d++) { double *p = &ph[j * 2 + d]; *p += c[j] * (d ? 1.006 : 0.994) / SR; *p -= floor(*p); if (d) s2 += 2 * *p - 1; else s += 2 * *p - 1; }
        lp += (s - lp) * 0.08; lp2 += (s2 - lp2) * 0.08;
        double t = (double)(i % beat) / SR, kick = sin(2 * M_PI * (45 * t + 120 * (1 - exp(-t * 35)) / 35)) * exp(-t * 9) * ((i / beat) % 2 ? 0.5 : 1.0);
        sc[i] = (int16_t)(kick * 28000);
        dl[i] = (int16_t)(lp * 0.2 * 32767); dr[i] = (int16_t)(lp2 * 0.2 * 32767);
    }
    memcpy(wl, dl, N * 2); memcpy(wr, dr, N * 2);

    op1_beat_t *b = host_beat(); b->len = (int32_t)((int64_t)SR * 60 / BPM * 16384); b->step = OP1_FX_BLOCK * 16384; b->follows_tape = 0;
    int32_t next = b->len;
#ifdef STEREO
    FX.mono.init(2);
#else
    FX.init(2);
#endif
    int64_t clip = 0; double rms_in = 0, rms_out = 0;
    for (long i = 0; i + OP1_FX_BLOCK <= N; i += OP1_FX_BLOCK) {
        b->next = next; sc_pos = i;
#ifdef STEREO
        FX.render_stereo(2, wl + i, wr + i, k);
#else
        FX.render(2, wl + i, k); memcpy(wr + i, wl + i, OP1_FX_BLOCK * 2);
#endif
        next -= OP1_FX_BLOCK * 16384; if (next <= 0) next += b->len;
        for (int j = 0; j < OP1_FX_BLOCK; j++) {
            if (wl[i + j] == 32767 || wl[i + j] == -32768) clip++;
            rms_in += (double)dl[i + j] * dl[i + j]; rms_out += (double)wl[i + j] * wl[i + j];
        }
    }
    wav("dry.wav", dl, dr, N); wav("wet.wav", wl, wr, N);
    printf("knobs %d %d %d %d, page 2 %d %d %d: level in %.1f dB, out %.1f dB, %lld clipped samples. Wrote dry.wav and wet.wav.\n",
           k[0], k[1], k[2], k[3], k[5], k[6], k[7], 10 * log10(rms_in / N / 1073741824.0 + 1e-12), 10 * log10(rms_out / N / 1073741824.0 + 1e-12), (long long)clip);
    return 0;
}
