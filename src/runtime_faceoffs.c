/* Face-offs and tour objectives in private lobby races.
 *
 * Face-offs: the game's face-offs ("social goals": beat a friend's score on a stretch of the track,
 * scripts/lua/libraries/socialgoals.lua) run in single events and the tour; a lobby race's config says kNoFaceOffs
 * (GameConfig:GetFaceOffs() == 2), so the race's Lua (stm/mission/race socialgoals/init.lua) turns them off. probe.c
 * patches the native code that hands the config to Lua: while this switch is on, faceOffs is 0 (kAllFaceOffs).
 *
 * Objectives (runtime_objectives.c) are told about each lobby race by runtime_race_config_hook, which the config
 * builder calls (see probe.c).
 *
 * Both are switched in the online menu and kept in <user>/faceoffs.ini (on=, objectives=; BB_FACEOFFS and
 * BB_OBJECTIVES override). Both on by default. */
#include "runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_FACEOFFS 0x360      /* uint32: 0 all, 1 tour only, 2 none (lobby races) */
#define CFG_TRACK_FN 0x4f70      /* const char *track(config) */

/* Read by the patched code as a byte: nonzero on. 2 until the choice is read (on, the default). */
volatile uint8_t runtime_faceoffs_on = 2;
static volatile uint8_t objectives_on = 2;

static void ini_path(char *path, size_t size) {
    const char *user = runtime_file_user_dir();
    snprintf(path, size, "%s/faceoffs.ini", user && *user ? user : ".");
}

static void load(void) {
    const char *user = runtime_file_user_dir();
    if (!user || !*user) return; /* not configured yet */
    int faceoffs = 1, objectives = 1;
    char path[600], line[32];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "on=", 3)) faceoffs = atoi(line + 3) != 0;
            if (!strncmp(line, "objectives=", 11)) objectives = atoi(line + 11) != 0;
        }
        fclose(f);
    }
    const char *e = getenv("BB_FACEOFFS");
    if (e && *e) faceoffs = atoi(e) != 0;
    e = getenv("BB_OBJECTIVES");
    if (e && *e) objectives = atoi(e) != 0;
    __atomic_store_n(&objectives_on, (uint8_t)objectives, __ATOMIC_RELEASE);
    __atomic_store_n(&runtime_faceoffs_on, (uint8_t)faceoffs, __ATOMIC_RELEASE);
}

static void save(void) {
    char path[600];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "on=%d\nobjectives=%d\n", runtime_faceoffs_on != 0, objectives_on != 0); fclose(f); }
}

int runtime_faceoffs_enabled(void) {
    if (__atomic_load_n(&runtime_faceoffs_on, __ATOMIC_ACQUIRE) == 2) load();
    return __atomic_load_n(&runtime_faceoffs_on, __ATOMIC_ACQUIRE) != 0;
}

int runtime_objectives_enabled(void) {
    if (__atomic_load_n(&objectives_on, __ATOMIC_ACQUIRE) == 2) load();
    return __atomic_load_n(&objectives_on, __ATOMIC_ACQUIRE) != 0;
}

void runtime_faceoffs_toggle(void) {
    const int on = !runtime_faceoffs_enabled();
    runtime_objectives_enabled();
    __atomic_store_n(&runtime_faceoffs_on, (uint8_t)on, __ATOMIC_RELEASE);
    save();
    printf("Face-offs: %s in every race (from the next race)\n", on ? "on" : "off as the game has them");
    fflush(stdout);
}

void runtime_objectives_toggle(void) {
    const int on = !runtime_objectives_enabled();
    runtime_faceoffs_enabled();
    __atomic_store_n(&objectives_on, (uint8_t)on, __ATOMIC_RELEASE);
    save();
    printf("Objectives: tour objectives in lobby races %s (from the next race)\n", on ? "on" : "off");
    fflush(stdout);
}

/* The config builder (probe.c) calls this for every race config it hands to Lua. A lobby race's says kNoFaceOffs;
 * the builder may run more than once per race, so a race is only taken once the face-off value is still the lobby's
 * (the thunk at the faceOffs load leaves the config itself as it is). */
ABI void runtime_race_config_hook(uint8_t *config) {
    if (*(const uint32_t *)(config + CFG_FACEOFFS) != 2) return; /* not a lobby race */
    const char *(ABI *track_of)(void *) = (const char *(ABI *)(void *))(runtime_image_base + CFG_TRACK_FN);
    const char *track = track_of(config);
    runtime_objectives_race(track);
}
