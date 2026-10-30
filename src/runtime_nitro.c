/* Nitro for every car: the game's own boost ("KERS": X / Cross, a charge that runs out and fills again, its sound
 * and gauge), which only the Wombat Typhoon, the McLaren P1 and P1 GTR and the Jaguar C-X75 have.
 *
 * Whether a car has it is one byte of its physics definition (vehicledefs.vdef, loaded at start): the definitions
 * are a vector at *(image + 0x1915fa0) + 8 (begin, end), 0xe20 bytes each, the name first; the byte at +0x610 is 1 for
 * those four cars and 0 for every other (the boost's parameters next to it, +0x5f0..+0x60c, are the same for all).
 * A car takes its definition when it is put on the track, so the bytes set here count from the next race on. Off,
 * every definition gets its own value back. The online menu picks off, on (the charge runs out and fills again) or
 * unlimited; the choice is kept in <user>/nitro.ini (BB_NITRO=0, 1 or 2 overrides it). The overlay draws the charge
 * (runtime_nitro_state): turquoise, purple when unlimited. Tested: a Ferrari 458 reaches 154 km/h in 5 s with X held
 * against 95 km/h without (the game's own boost alone gave it nothing, see push()). */
#include "runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VDEF_HOLDER 0x1915fa0
#define VDEF_SIZE 0xe20
#define VDEF_BOOST 0x610
#define VDEF_MAX 512
#define HEAP_LO 0x2000000000ull
#define HEAP_HI 0x2100000000ull

static volatile int32_t enabled = -1; /* -1: not read from BB_NITRO yet */
static uint8_t original[VDEF_MAX];
static int saved; /* definitions whose own value is in original[] */

static void ini_path(char *path, size_t size) {
    const char *user = runtime_file_user_dir();
    snprintf(path, size, "%s/nitro.ini", user && *user ? user : ".");
}

int runtime_nitro_enabled(void) {
    if (__atomic_load_n(&enabled, __ATOMIC_ACQUIRE) < 0) {
        int on = 0;
        const char *e = getenv("BB_NITRO");
        if (e && *e) {
            on = atoi(e);
        } else {
            char path[600], line[32];
            ini_path(path, sizeof path);
            FILE *f = fopen(path, "rb");
            if (f) {
                while (fgets(line, sizeof line, f)) if (!strncmp(line, "on=", 3)) on = atoi(line + 3);
                fclose(f);
            }
        }
        __atomic_store_n(&enabled, on < 0 || on > 2 ? 0 : on, __ATOMIC_RELEASE);
    }
    return __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
}

/* The next mode up (dir > 0) or down, round: off, on, unlimited. */
void runtime_nitro_step(int dir) {
    const int on = (runtime_nitro_enabled() + (dir < 0 ? 2 : 1)) % 3;
    __atomic_store_n(&enabled, on, __ATOMIC_RELEASE);
    char path[600];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "on=%d\n", on); fclose(f); }
    printf("Nitro: %s (from the next race)\n", on == 2 ? "unlimited for every car" : on ? "on for every car" : "off");
    fflush(stdout);
}

static int heap(uint64_t v) { return v >= HEAP_LO && v < HEAP_HI; }

/* The push. The byte above gives a car the game's boost state, gauge and sound, but the force itself comes from the
 * hybrid drive only the four boost cars have in their physics: on any other car the charge never drains and nothing
 * pushes. For those the push is added here, while X is held and our own charge lasts, along the car's forward axis.
 * The player's car: G = *(image + 0x1911100), vehicle = *(*(G + 0x6c8) + 0x838) (actor + 0x200), its two Havok
 * bodies at +0x1d8 and +0x398 (rotation column +0x190 forward, linear velocity +0x230); its definition
 * *(*(*(actor + 0x3d8) + 0xb8) + 0x18) tells whether it boosts by itself. */
#define RIGID_BODY_VTABLE 0x160d6f0
#define PUSH_ACCEL 6.5f      /* m/s^2 on top of the car's own */
#define PUSH_SECONDS 4.0f    /* a full charge lasts this long */
#define PUSH_REFILL 12.0f    /* and comes back in this long, not while X is held */
#define PUSH_MIN_SPEED 3.0f  /* m/s: not from standing still (it would spin the car on the grid) */
#define BTN_CROSS 0x4000

static float push_charge = 1.0f;
static volatile int32_t push_car, push_firing; /* for the overlay: a pushed car is driven, and it is firing now */

/* The vehicle state's KERSCharge getter (guest 0xc4c660, replaced in probe.c): the game's own value is
 * *(*(this->vtable[0x350](this) + 8) + 0xd04); for a car we push, our charge instead, so the game's boost gauge
 * runs down and fills like the Wombat's. */
typedef ABI uint64_t (*vstate_fn)(void *self);
ABI float runtime_nitro_charge_hook(void *self) {
    const uint64_t x = ((vstate_fn)(*(uint64_t **)self)[0x350 / 8])(self);
    const float own = *(const float *)(*(const uint64_t *)(x + 8) + 0xd04);
    static int logged;
    if (!logged++) { printf("Nitro: the game asks for the boost charge (gauge)\n"); fflush(stdout); }
    return __atomic_load_n(&push_car, __ATOMIC_ACQUIRE) ? push_charge : own;
}

/* For the overlay's bar: 0 when there is none to draw, else the charge (0..1), whether it fires and whether it is
 * unlimited. */
int runtime_nitro_state(float *charge, int *firing, int *unlimited) {
    *charge = push_charge;
    *firing = __atomic_load_n(&push_firing, __ATOMIC_ACQUIRE);
    *unlimited = runtime_nitro_enabled() == 2;
    return runtime_nitro_enabled() && __atomic_load_n(&push_car, __ATOMIC_ACQUIRE);
}

static float *body_of(uint64_t vehicle, int which) {
    const uint64_t b = *(const uint64_t *)(vehicle + (which ? 0x398 : 0x1d8));
    return heap(b) && *(const uint64_t *)b == runtime_image_base + RIGID_BODY_VTABLE ? (float *)b : NULL;
}

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

static void push(uint32_t buttons, uint64_t begin, int count) {
    static uint64_t last;
    const uint64_t t = now_us();
    const float dt = last ? fminf((float)(t - last) / 1e6f, 0.05f) : 0.0f;
    last = t;
    const uint64_t g = *(const uint64_t *)(runtime_image_base + 0x1911100);
    const uint64_t holder = g ? *(const uint64_t *)(g + 0x6c8) : 0; /* G is not on the guest heap */
    const uint64_t vehicle = heap(holder) ? *(const uint64_t *)(holder + 0x838) : 0;
    /* push_car is read by the gauge on another thread: it only changes when the car does */
    if (!heap(vehicle)) { push_charge = 1.0f; __atomic_store_n(&push_car, 0, __ATOMIC_RELEASE); return; }
    const uint64_t a = *(const uint64_t *)(vehicle - 0x200 + 0x3d8);
    const uint64_t b = heap(a) ? *(const uint64_t *)(a + 0xb8) : 0;
    const uint64_t def = heap(b) ? *(const uint64_t *)(b + 0x18) : 0;
    const int unlimited = runtime_nitro_enabled() == 2;
    float *b0 = body_of(vehicle, 0), *b1 = body_of(vehicle, 1);
    /* the four boost cars drive themselves; unlimited, they get the push as well (their own charge runs out) */
    const int ours = def >= begin && def < begin + (uint64_t)count * VDEF_SIZE && !((def - begin) % VDEF_SIZE) && b0 &&
                     (!original[(def - begin) / VDEF_SIZE] || unlimited);
    __atomic_store_n(&push_car, ours, __ATOMIC_RELEASE);
    if (!ours || dt <= 0) { __atomic_store_n(&push_firing, 0, __ATOMIC_RELEASE); return; }
    if (unlimited) push_charge = 1.0f;
    const float *fwd = b0 + 0x190 / 4, *vel = b0 + 0x230 / 4;
    const float speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    const float ahead = vel[0] * fwd[0] + vel[1] * fwd[1] + vel[2] * fwd[2];
    const int held = (buttons & BTN_CROSS) != 0;
    const int firing = held && push_charge > 0.0f && speed > PUSH_MIN_SPEED && ahead > 0;
    __atomic_store_n(&push_firing, firing, __ATOMIC_RELEASE);
    if (firing) {
        for (int k = 0; k < 3; ++k) {
            const float dv = fwd[k] * PUSH_ACCEL * dt;
            b0[0x230 / 4 + k] += dv;
            if (b1) b1[0x230 / 4 + k] += dv;
        }
        if (!unlimited) push_charge = fmaxf(0.0f, push_charge - dt / PUSH_SECONDS);
    } else if (!held) {
        push_charge = fminf(1.0f, push_charge + dt / PUSH_REFILL);
    }
    static int was, logged;
    if (firing != was && logged++ < 12) { printf("Nitro: push %s (charge %.2f)\n", firing ? "on" : "off", push_charge); fflush(stdout); }
    was = firing;
}

/* Called on the game's thread with every pad read (the buttons the game got): keeps the definitions' boost bytes as
 * the toggle says, and pushes cars without a boost of their own. */
void runtime_nitro_frame(uint32_t buttons) {
    if (!runtime_image_base) return;
    const uint64_t holder = *(const uint64_t *)(runtime_image_base + VDEF_HOLDER);
    if (!heap(holder)) return;
    const uint64_t begin = *(const uint64_t *)(holder + 8), end = *(const uint64_t *)(holder + 0x10);
    if (!heap(begin) || end <= begin || (end - begin) % VDEF_SIZE) return;
    const int count = (int)((end - begin) / VDEF_SIZE);
    if (count > VDEF_MAX) return;
    uint8_t *flag0 = (uint8_t *)(begin + VDEF_BOOST);
    if (saved != count) { /* first time (or the table changed): remember each car's own value */
        int with = 0;
        for (int i = 0; i < count; ++i) { original[i] = flag0[(size_t)i * VDEF_SIZE]; with += original[i] != 0; }
        saved = count;
        printf("Nitro: %d vehicle definitions, %d with boost of their own\n", count, with);
        fflush(stdout);
    }
    const int on = runtime_nitro_enabled();
    for (int i = 0; i < count; ++i) flag0[(size_t)i * VDEF_SIZE] = on ? 1 : original[i];
    if (on) push(buttons, begin, count);
}
