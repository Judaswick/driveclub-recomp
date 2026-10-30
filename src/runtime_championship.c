/* Championships in a private lobby. np_handler.cpp hands over each race's lobby settings (the 60-byte block at
 * guest 0x18c31c0) as the co-op server says; the game's own thread (the pad read) applies them once the private
 * lobby is back in its idle state: into the block and through the lobby manager's setter (0x4cbb10: its copy at
 * +0x378, which the game copies back into the block on the way back to the lobby and sends to the guests). From
 * the second race on it then starts the race by itself, as the host's "Start Event" dialog does: the lobby
 * manager (guest [[0x1911100]+0x970]+0x7c0, +0x18) goes to state 2 (0x4cb150). */
#include "runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define LOBBY_SETTINGS 0x18c31c0
#define LOBBY_SETTINGS_SIZE 60
#define CHAMPIONSHIP_SETTLE_MS 300 /* idle before the race is set */
#define CHAMPIONSHIP_START_MS 1200 /* then at least this long before it starts */
#define CHAMPIONSHIP_WAIT_MAX_MS 30000 /* and at most this long for every member to be ready */

typedef void (ABI *lobby_set_state)(void *manager, int32_t state);
typedef void (ABI *lobby_set_settings)(void *manager, const void *settings);

static volatile int32_t race_pending; /* 1: race_block waits to be applied */
static volatile int32_t start_pending;
static unsigned char race_block[LOBBY_SETTINGS_SIZE];

/* A race to put into the lobby (block NULL: nothing to apply, a pending start is taken back too). */
void runtime_championship_set_race(const unsigned char *block, int autostart) {
    __atomic_store_n(&race_pending, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&start_pending, 0, __ATOMIC_RELEASE);
    if (!block) return;
    memcpy(race_block, block, sizeof race_block);
    __atomic_store_n(&start_pending, autostart ? 1 : 0, __ATOMIC_RELEASE);
    __atomic_store_n(&race_pending, 1, __ATOMIC_RELEASE);
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint8_t *lobby_manager(void) {
    uint8_t *root = *(uint8_t **)(runtime_image_base + 0x1911100);
    if (!root) return NULL;
    uint8_t *services = *(uint8_t **)(root + 0x970);
    if (!services) return NULL;
    uint8_t *lobby = *(uint8_t **)(services + 0x7c0);
    return lobby ? lobby + 0x18 : NULL;
}

/* Called on the game's thread with every pad read. */
void runtime_championship_frame(void) {
    static uint64_t idle_since, applied_at;
    if (!runtime_image_base) return;
    {
        uint8_t *m = lobby_manager();
        runtime_botsync_frame(m ? *(int32_t *)(m + 0x3b4) : 0, runtime_bots_is_host());
    }
    const int pending = __atomic_load_n(&race_pending, __ATOMIC_ACQUIRE);
    const int start = __atomic_load_n(&start_pending, __ATOMIC_ACQUIRE);
    if (!pending && !start) { idle_since = applied_at = 0; return; }
    uint8_t *manager = lobby_manager();
    if (!manager || *(int32_t *)(manager + 0x3b4) != 0) { idle_since = applied_at = 0; return; }
    const uint64_t t = now_ms();
    if (!idle_since) idle_since = t;
    if (pending) {
        if (t - idle_since < CHAMPIONSHIP_SETTLE_MS) return;
        memcpy((void *)(runtime_image_base + LOBBY_SETTINGS), race_block, sizeof race_block);
        ((lobby_set_settings)(runtime_image_base + 0x4cbb10))(manager, race_block);
        __atomic_store_n(&race_pending, 0, __ATOMIC_RELEASE);
        applied_at = t;
        printf("Championship: race settings applied\n");
        fflush(stdout);
        return;
    }
    if (!applied_at) applied_at = t;
    if (t - applied_at < CHAMPIONSHIP_START_MS) return;
    /* Every member back and on the new settings: a member's state (+i*0x48, as the lobby's own ready check
     * 0x382b10 reads it) goes above 1 once it has them; the setter set the guests' to 0. */
    const int members = *(const uint8_t *)(manager + 0x360);
    int ready = 0;
    for (int i = 0; i < members; ++i) ready += *(const uint16_t *)(manager + i * 0x48) > 1;
    if (ready < members && t - applied_at < CHAMPIONSHIP_WAIT_MAX_MS) return;
    __atomic_store_n(&start_pending, 0, __ATOMIC_RELEASE);
    idle_since = applied_at = 0;
    printf("Championship: starting the next race (%d of %d members ready)\n", ready, members);
    fflush(stdout);
    ((lobby_set_state)(runtime_image_base + 0x4cb150))(manager, 2);
}
/* Bots in private lobby races. The lobby builds the race's entrants on every machine (0x37d240: one
 * AddEntrant 0x3ff0 per member, then 0x3da0(setup, 3)); the single event adds its AI the same way, as
 * "racer" with flags 4. The co-op server says how many bots the lobby has (F1 on the host). The game sends
 * no AI cars over the network (remote AI entrants stay invisible), so every game drives the bots itself
 * (+0x15c 1, local), and runtime_botsync.c keeps the guests' on the host's states. Each drives another base game car of
 * the host's car's class (the host is the first entrant), picked from the track and that car, so every game
 * picks the same. */
typedef void (ABI *add_entrant_fn)(void *setup, int32_t a, const char *kind, int32_t vehicle, void *livery,
                                   int32_t flags, float skill, int64_t s0, int64_t s1, int64_t s2, int64_t s3,
                                   int64_t s4, int64_t s5, int64_t s6);
typedef void (ABI *entrants_done_fn)(void *setup, int32_t arg);
typedef void (ABI *ai_difficulty_fn)(void *setup, int32_t difficulty);

static volatile int32_t ai_difficulty = -1; /* -1: the game's own (see runtime_ai_difficulty_cycle) */

#include "runtime_bot_cars.inc"

static volatile int32_t bot_count, bot_host;
static int32_t bot_vehicle = -1;

int runtime_bots_count(void) { return __atomic_load_n(&bot_count, __ATOMIC_ACQUIRE); }
int runtime_bots_is_host(void) { return __atomic_load_n(&bot_host, __ATOMIC_ACQUIRE); }

void runtime_bots_set(int count, int host) {
    __atomic_store_n(&bot_count, count < 0 ? 0 : count > 11 ? 11 : count, __ATOMIC_RELEASE);
    __atomic_store_n(&bot_host, host ? 1 : 0, __ATOMIC_RELEASE);
}

ABI void runtime_bots_entrant_hook(void *setup, int32_t a, const char *kind, int32_t vehicle, void *livery,
                                   int32_t flags, float skill, int64_t s0, int64_t s1, int64_t s2, int64_t s3,
                                   int64_t s4, int64_t s5, int64_t s6) {
    if (bot_vehicle < 0) bot_vehicle = vehicle; /* the first entrant: the host (lobby order), the same everywhere */
    ((add_entrant_fn)(runtime_image_base + 0x3ff0))(setup, a, kind, vehicle, livery, flags, skill, s0, s1, s2, s3,
                                                    s4, s5, s6);
}

/* The car of bot i: a base game car of the host car's class, the same choice in every game (from the track,
 * the host's car and i); the host's car itself when it is not in the list. */
static int32_t bot_car(int32_t i, int32_t count) {
    int cls = -1;
    for (size_t k = 0; k < sizeof bot_cars / sizeof *bot_cars; ++k)
        if (bot_cars[k].id == bot_vehicle) cls = bot_cars[k].cls;
    int16_t same[64];
    int n = 0;
    for (size_t k = 0; k < sizeof bot_cars / sizeof *bot_cars && n < 64; ++k)
        if (bot_cars[k].cls == cls) same[n++] = bot_cars[k].id;
    if (cls < 0 || !n) return bot_vehicle;
    uint32_t track = 0;
    memcpy(&track, (const void *)(runtime_image_base + LOBBY_SETTINGS + 4), 4);
    /* distinct cars while the class has enough: probe on from the hashed start */
    static int8_t used[64];
    if (i == 0) memset(used, 0, sizeof used);
    uint32_t h = (track * 2654435761u) ^ ((uint32_t)bot_vehicle * 40503u) ^ ((uint32_t)i * 2246822519u);
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    int pick = (int)(h % (uint32_t)n);
    for (int tries = 0; tries < n && used[pick] && count <= n; ++tries) pick = (pick + 1) % n;
    used[pick] = 1;
    return same[pick];
}

ABI void runtime_bots_done_hook(void *setup, int32_t arg) {
    const int32_t count = __atomic_load_n(&bot_count, __ATOMIC_ACQUIRE);
    for (int32_t i = 0; i < count && bot_vehicle >= 0; ++i) {
        unsigned char livery[20] = {1}; /* as the single event's: byte 0 set, default paint */
        static const char paint[] = "default";
        ((add_entrant_fn)(runtime_image_base + 0x3ff0))(setup, 0, (const char *)(runtime_image_base + 0x12ad437),
                                                        bot_car(i, count), livery, 4, 0.0f, 1, -1, i + 1, -1,
                                                        (int64_t)(uintptr_t)paint, 0, 0);
    }
    if (count) {
        printf("Bots: %d added to the race (vehicle %d, %s)\n", count, bot_vehicle,
               __atomic_load_n(&bot_host, __ATOMIC_ACQUIRE) ? "host" : "guest");
        fflush(stdout);
    }
    if (count) { /* the lobby never sets its AI's skill: as a single event does, after the entrants */
        const int32_t chosen = __atomic_load_n(&ai_difficulty, __ATOMIC_ACQUIRE);
        ((ai_difficulty_fn)(runtime_image_base + 0x5360))(setup, chosen >= 0 ? chosen : 3);
    }
    bot_vehicle = -1;
    ((entrants_done_fn)(runtime_image_base + 0x3da0))(setup, arg);
}
/* AI difficulty. The game sets its AI's skill after adding the entrants: 0x5360(setup, difficulty), 0 Rookie
 * to 4 Legend (the single event's setting, a tour event's own). The online menu picks one for every race: the
 * game's own (the default) or a fixed one; the private lobby's bots, which the game never sets, get it too
 * (Professional while it is the game's own). */
const char *runtime_ai_difficulty_name(void) {
    static const char *const names[] = {"Game's own", "Rookie", "Amateur", "Semi-Pro", "Professional", "Legend"};
    return names[__atomic_load_n(&ai_difficulty, __ATOMIC_ACQUIRE) + 1];
}

/* One step up (dir > 0) or down, from the game's own to Legend. */
void runtime_ai_difficulty_step(int dir) {
    int32_t next = __atomic_load_n(&ai_difficulty, __ATOMIC_ACQUIRE) + (dir > 0 ? 1 : -1);
    if (next > 4) next = 4;
    if (next < -1) next = -1;
    __atomic_store_n(&ai_difficulty, next, __ATOMIC_RELEASE);
    printf("AI difficulty: %d\n", next);
    fflush(stdout);
}

ABI void runtime_ai_difficulty_hook(void *setup, int32_t difficulty) {
    const int32_t chosen = __atomic_load_n(&ai_difficulty, __ATOMIC_ACQUIRE);
    ((ai_difficulty_fn)(runtime_image_base + 0x5360))(setup, chosen >= 0 ? chosen : difficulty);
}