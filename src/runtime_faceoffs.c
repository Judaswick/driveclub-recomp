/* Face-offs in private lobby races. The game's face-offs ("social goals": beat a friend's score on a stretch of the
 * track, scripts/lua/libraries/socialgoals.lua) run in single events and the tour; a lobby race's config says
 * kNoFaceOffs (GameConfig:GetFaceOffs() == 2), so the race's Lua (stm/mission/race socialgoals/init.lua) turns them
 * off. probe.c patches the native code that hands the config to Lua: while this switch is on, faceOffs is 0
 * (kAllFaceOffs) for every race. The online menu turns it on and off; the choice is kept in <user>/faceoffs.ini
 * (BB_FACEOFFS=0 or 1 overrides it). On by default. */
#include "runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read by the patched code as a byte: nonzero on. 2 until the choice is read (on, the default). */
volatile uint8_t runtime_faceoffs_on = 2;

static void ini_path(char *path, size_t size) {
    const char *user = runtime_file_user_dir();
    snprintf(path, size, "%s/faceoffs.ini", user && *user ? user : ".");
}

int runtime_faceoffs_enabled(void) {
    if (__atomic_load_n(&runtime_faceoffs_on, __ATOMIC_ACQUIRE) == 2) {
        const char *user = runtime_file_user_dir();
        if (!user || !*user) return 1; /* not configured yet */
        int on = 1;
        const char *e = getenv("BB_FACEOFFS");
        if (e && *e) {
            on = atoi(e) != 0;
        } else {
            char path[600], line[32];
            ini_path(path, sizeof path);
            FILE *f = fopen(path, "rb");
            if (f) {
                while (fgets(line, sizeof line, f)) if (!strncmp(line, "on=", 3)) on = atoi(line + 3) != 0;
                fclose(f);
            }
        }
        __atomic_store_n(&runtime_faceoffs_on, (uint8_t)on, __ATOMIC_RELEASE);
    }
    return __atomic_load_n(&runtime_faceoffs_on, __ATOMIC_ACQUIRE) != 0;
}

void runtime_faceoffs_toggle(void) {
    const int on = !runtime_faceoffs_enabled();
    __atomic_store_n(&runtime_faceoffs_on, (uint8_t)on, __ATOMIC_RELEASE);
    char path[600];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "on=%d\n", on); fclose(f); }
    printf("Face-offs: %s in every race (from the next race)\n", on ? "on" : "off as the game has them");
    fflush(stdout);
}
