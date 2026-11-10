/* Host mocks for testing a kennel FX plugin on your computer (no OP-1, no emulator).
 * op1_plugin.h includes this file when OP1_HOST is defined. It stands in for the firmware's tables,
 * the encoder state and the screen. Part of Duck Call's fx-host kit, GPL-3.0-or-later (see the repo's LICENSE). */
#include <stdint.h>
static int16_t host_vel[8];
static int16_t host_sine[8192];                     /* fx_host.c fills it: one full sine, Q15 */
#define OP1_VOICE_VEL   ((const volatile int16_t *)host_vel)
#define OP1_VOICE_BASE  ((uintptr_t)0)
#define OP1_SINE_TABLE  ((const int16_t *)host_sine)
#define OP1_FX_MASTER   2
/* knob words for the screen, and the screen itself (screens are not drawn here; svg_check.py checks the art) */
static int16_t host_fx_knobs[3][8];
static int host_pos = 2;
#define op1_node_set_transform(n, m) ((void)(n), (void)(m))
#define op1_raster() ((void *)0)
#define op1_raster_line(r, a, b, w, c) ((void)(r), (void)(a), (void)(b), (void)(w), (void)(c))
#define op1_screen_draw(s, d) ((void)(s), (void)(d))
#define op1_fx_screen_pos() host_pos
#define op1_fx_knobs(pos) ((const int16_t *)host_fx_knobs[(pos)])
static inline void op1_fx_knobs_live(int16_t out[4]) { for (int i = 0; i < 4; i++) out[i] = host_fx_knobs[host_pos][i]; }
