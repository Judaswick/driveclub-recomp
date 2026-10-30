/* Nitro for every car: the game's own boost ("KERS": X / Cross, a charge that runs out and fills again, its sound
 * and gauge), which only the Wombat Typhoon, the McLaren P1 and P1 GTR and the Jaguar C-X75 have.
 *
 * Whether a car has it is one byte of its physics definition (vehicledefs.vdef, loaded at start): the definitions
 * are a vector at *(image + 0x1915fa0) + 8 (begin, end), 0xe20 bytes each, the name first; the byte at +0x610 is 1 for
 * those four cars and 0 for every other (the boost's parameters next to it, +0x5f0..+0x60c, are the same for all).
 * A car takes its definition when it is put on the track, so the bytes set here count from the next race on. Off,
 * every definition gets its own value back. The online menu turns it on and off; the choice is kept in
 * <user>/nitro.ini (BB_NITRO=1 or 0 overrides it). Tested: a Ferrari 458 reaches 149 km/h in 5 s with X held
 * against 99 km/h without. */
#include "runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
            on = !strcmp(e, "1");
        } else {
            char path[600], line[32];
            ini_path(path, sizeof path);
            FILE *f = fopen(path, "rb");
            if (f) {
                while (fgets(line, sizeof line, f)) if (!strncmp(line, "on=", 3)) on = atoi(line + 3) == 1;
                fclose(f);
            }
        }
        __atomic_store_n(&enabled, on, __ATOMIC_RELEASE);
    }
    return __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
}

void runtime_nitro_toggle(void) {
    const int on = !runtime_nitro_enabled();
    __atomic_store_n(&enabled, on, __ATOMIC_RELEASE);
    char path[600];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "on=%d\n", on); fclose(f); }
    printf("Nitro: %s (from the next race)\n", on ? "on for every car" : "off");
    fflush(stdout);
}

static int heap(uint64_t v) { return v >= HEAP_LO && v < HEAP_HI; }

/* Called on the game's thread with every pad read: keeps the definitions' boost bytes as the toggle says. */
void runtime_nitro_frame(void) {
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
}
