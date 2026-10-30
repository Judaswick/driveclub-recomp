/* Bots in private lobby races, kept the same in every game. Every game drives its bots itself (the game sends
 * no AI cars over the network, runtime_championship.c); for each bot one game is in charge and sends its
 * state ~30 times a second to the others (np_handler.cpp), which put it on their bot of the same name. The
 * host is in charge of a bot unless a player comes close to it (6 m; let go beyond 10 m): then that player's
 * game is, so a bump with that player's car is worked out where it happens and everyone sees it. The host
 * decides and tells the others with its packets.
 *
 * Finding the cars: a driver's record holds the name twice (std::string, inline: text, size at +0x10,
 * capacity 15 at +0x18; the second copy at +0x28) and a vehicle that points at its two Havok rigid bodies at
 * +0x1d8 and +0x398 (vtable 0x160d6f0): an AI driver's at name +0x200, a player's at name +0x150. The guest
 * heap (0x2000000000-0x2100000000) is searched for those once a race is on.
 *
 * A body's state (hkpRigidBody, 64-bit): rotation rows +0x170/+0x180/+0x190, position +0x1a0, swept centre
 * of mass +0x1b0/+0x1c0 (their w: the step's times, kept), swept rotation +0x1d0/+0x1e0, linear velocity
 * +0x230, angular velocity +0x240. */
#include "../gpu/shim/bbport_diagnostics.h"
#include "runtime.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define HEAP_LO 0x2000000000ull
#define HEAP_HI 0x2100000000ull
#define RIGID_BODY_VTABLE 0x160d6f0
#define MAX_CARS 16
#define NAME_LEN 24
#define BODY_FLOATS 32
#define PACKET_MAGIC 0x32544f42u /* "BOT2" */
#define TAKE_OVER_M 6.0f
#define LET_GO_M 10.0f
#include <stdlib.h>

/* BB_BOTSYNC_RANGE=<m> (diagnostics): the distance at which a player takes a bot over, for testing. */
static float take_over_m(void) {
    static float m = -1;
    if (m < 0) {
        const char *e = BB_DIAGNOSTIC_ENV("BB_BOTSYNC_RANGE");
        m = e && atof(e) > 0 ? (float)atof(e) : TAKE_OVER_M;
    }
    return m;
}

typedef struct {
    char name[NAME_LEN];
    uintptr_t vehicle;
    int bot;
} car_ref;

typedef struct {
    char name[NAME_LEN];
    char owner[NAME_LEN];
    uint8_t has_state;
    float body[2][BODY_FLOATS];
} entry;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static car_ref refs[MAX_CARS];
static int ref_count, bots_found;
static char owners[MAX_CARS][NAME_LEN]; /* by refs index; the host's decision */
static entry pending[MAX_CARS];         /* states from others, newest per bot */
static int pending_fresh[MAX_CARS], pending_count;
static uint64_t pending_ms[MAX_CARS]; /* when each state came */
static char local_name[NAME_LEN];
static volatile int32_t racing; /* set by runtime_botsync_frame from the lobby state */

void runtime_botsync_set_local(const char *name) {
    pthread_mutex_lock(&lock);
    snprintf(local_name, NAME_LEN, "%s", name ? name : "");
    pthread_mutex_unlock(&lock);
}

/* Reads guest memory that may have gone away (a finished race's objects). */
static int safe_read(uintptr_t at, void *out, size_t n) {
#ifdef _WIN32
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)at, out, n, &got) && got == n;
#else
    memcpy(out, (const void *)at, n);
    return 1;
#endif
}

static int is_body(uint64_t b) {
    uint64_t vt = 0;
    return b >= HEAP_LO && b < HEAP_HI && !(b & 0xf) && safe_read(b, &vt, 8) &&
           vt == runtime_image_base + RIGID_BODY_VTABLE;
}

static int vehicle_bodies(uintptr_t vehicle, uint64_t body[2]) {
    return vehicle >= HEAP_LO && vehicle < HEAP_HI && safe_read(vehicle + 0x1d8, &body[0], 8) &&
           safe_read(vehicle + 0x398, &body[1], 8) && is_body(body[0]) && is_body(body[1]);
}

/* An inline std::string of 3..15 printable characters at s. */
static int short_name(const uint8_t *s) {
    const uint64_t size = *(const uint64_t *)(s + 0x10), cap = *(const uint64_t *)(s + 0x18);
    if (cap != 15 || size < 3 || size > 15 || s[size]) return 0;
    for (uint64_t i = 0; i < size; ++i)
        if (s[i] < 0x20 || s[i] > 0x7e) return 0;
    return 1;
}

static int scan(car_ref *out) {
    int n = 0;
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t at = HEAP_LO;
    while (at < HEAP_HI && VirtualQuery((const void *)at, &mbi, sizeof mbi) == sizeof mbi) {
        const uintptr_t base = (uintptr_t)mbi.BaseAddress, end = base + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            for (uintptr_t p = base; p + 0x208 <= end && n < MAX_CARS; p += 8) {
                const uint8_t *name = (const uint8_t *)p;
                if (name[0] < 0x21 || name[0] > 0x7e || !short_name(name) || !short_name(name + 0x28) ||
                    strcmp((const char *)name, (const char *)name + 0x28))
                    continue;
                uint64_t body[2], vehicle = *(const uint64_t *)(p + 0x200);
                int bot = 1;
                if (!vehicle_bodies(vehicle, body)) {
                    vehicle = *(const uint64_t *)(p + 0x150);
                    bot = 0;
                    if (!vehicle_bodies(vehicle, body)) continue;
                }
                int dup = 0;
                for (int i = 0; i < n; ++i) dup |= out[i].vehicle == vehicle;
                if (dup) continue;
                snprintf(out[n].name, NAME_LEN, "%s", (const char *)name);
                out[n].vehicle = vehicle;
                out[n].bot = bot;
                ++n;
            }
        }
        at = end;
    }
#endif
    return n;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* The search runs off the game's thread: every 2 s during a race until it found all bots, again when a race
 * starts. */
static void *scanner(void *arg) {
    (void)arg;
    int done = 0;
    uint64_t last = 0;
    for (;;) {
        struct timespec pause = {0, 500 * 1000000};
        nanosleep(&pause, NULL);
        const int race = __atomic_load_n(&racing, __ATOMIC_ACQUIRE);
        const int want = runtime_bots_count();
        if (!race || !want) {
            if (done || ref_count) {
                pthread_mutex_lock(&lock);
                ref_count = bots_found = pending_count = 0;
                memset(owners, 0, sizeof owners);
                pthread_mutex_unlock(&lock);
            }
            done = 0;
            continue;
        }
        if (done || now_ms() - last < 2000) continue;
        last = now_ms();
        car_ref found[MAX_CARS];
        const int n = scan(found);
        int bots = 0;
        for (int i = 0; i < n; ++i) bots += found[i].bot;
        pthread_mutex_lock(&lock);
        memcpy(refs, found, sizeof(car_ref) * (size_t)n);
        ref_count = n;
        bots_found = bots;
        memset(owners, 0, sizeof owners);
        pthread_mutex_unlock(&lock);
        printf("Bot sync: %d of %d bots found, %d cars:", bots, want, n);
        for (int i = 0; i < n; ++i) printf(" %s%s", found[i].name, found[i].bot ? "" : " (player)");
        printf("\n");
        fflush(stdout);
        done = bots >= want;
    }
    return NULL;
}

static void read_body(uint64_t body, float *f) {
    float v[4];
    int k = 0;
    static const uint32_t vec3[] = {0x170, 0x180, 0x190, 0x1a0, 0x1b0, 0x1c0};
    for (int i = 0; i < 6; ++i) {
        safe_read(body + vec3[i], v, 12);
        f[k++] = v[0]; f[k++] = v[1]; f[k++] = v[2];
    }
    safe_read(body + 0x1d0, f + k, 16); k += 4;
    safe_read(body + 0x1e0, f + k, 16); k += 4;
    safe_read(body + 0x230, f + k, 12); k += 3;
    safe_read(body + 0x240, f + k, 12); k += 3;
}

static void write_body(uint64_t body, const float *f) {
    float *b = (float *)body;
    int k = 0;
    static const uint32_t vec3[] = {0x170, 0x180, 0x190, 0x1a0, 0x1b0, 0x1c0};
    for (int i = 0; i < 6; ++i) { /* xyz only: the w of the centre of mass holds the step's times */
        float *v = b + vec3[i] / 4;
        v[0] = f[k++]; v[1] = f[k++]; v[2] = f[k++];
    }
    memcpy(b + 0x1d0 / 4, f + k, 16); k += 4;
    memcpy(b + 0x1e0 / 4, f + k, 16); k += 4;
    memcpy(b + 0x230 / 4, f + k, 12); k += 3;
    memcpy(b + 0x240 / 4, f + k, 12); k += 3;
}

static int car_position(const car_ref *c, float pos[3]) {
    uint64_t body[2];
    return vehicle_bodies(c->vehicle, body) && safe_read(body[0] + 0x1a0, pos, 12);
}

static float distance(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* Host: who is in charge of each bot (a player close to it, else the host). Call with lock held. */
static void decide_owners(void) {
    float pos[MAX_CARS][3];
    int valid[MAX_CARS];
    for (int i = 0; i < ref_count; ++i) valid[i] = car_position(&refs[i], pos[i]);
    for (int i = 0; i < ref_count; ++i) {
        if (!refs[i].bot || !valid[i]) continue;
        int nearest = -1, current = -1;
        float best = 1e9f;
        for (int j = 0; j < ref_count; ++j) {
            if (refs[j].bot || !valid[j]) continue;
            const float d = distance(pos[i], pos[j]);
            if (!strcmp(owners[i], refs[j].name) && d <= LET_GO_M + take_over_m() - TAKE_OVER_M) current = j;
            if (d < best) { best = d; nearest = j; }
        }
        const char *owner = current >= 0 ? refs[current].name
                            : nearest >= 0 && best <= take_over_m() ? refs[nearest].name
                                                                  : local_name;
        if (strcmp(owners[i], owner)) {
            printf("Bot sync: %s now driven by %s\n", refs[i].name, owner);
            fflush(stdout);
            snprintf(owners[i], NAME_LEN, "%s", owner);
        }
    }
}

/* The packet with what this game sends (0: nothing): the host every bot's owner and the states of those it is
 * in charge of; a guest the states of the bots it is in charge of. */
size_t runtime_botsync_capture(void *out, size_t cap) {
    static uint32_t seq;
    if (!__atomic_load_n(&racing, __ATOMIC_ACQUIRE)) return 0;
    const int host = runtime_bots_is_host();
    uint8_t *p = out;
    size_t used = 10;
    int count = 0;
    pthread_mutex_lock(&lock);
    if (host) decide_owners();
    for (int i = 0; i < ref_count; ++i) {
        if (!refs[i].bot) continue;
        const char *owner = owners[i][0] ? owners[i] : host ? local_name : "";
        const int mine = !strcmp(owner, local_name);
        if (!host && !mine) continue;
        if (used + sizeof(entry) > cap) break;
        entry *e = (entry *)(p + used);
        memset(e, 0, sizeof *e);
        memcpy(e->name, refs[i].name, NAME_LEN);
        snprintf(e->owner, NAME_LEN, "%s", owner);
        uint64_t body[2];
        if (mine && vehicle_bodies(refs[i].vehicle, body)) {
            read_body(body[0], e->body[0]);
            read_body(body[1], e->body[1]);
            e->has_state = 1;
        }
        used += sizeof(entry);
        ++count;
    }
    pthread_mutex_unlock(&lock);
    if (!count) return 0;
    const uint32_t magic = PACKET_MAGIC, s = ++seq;
    memcpy(p, &magic, 4);
    memcpy(p + 4, &s, 4);
    p[8] = (uint8_t)host;
    p[9] = (uint8_t)count;
    return used;
}

/* A packet from another game (any thread); the game's thread applies the newest states. */
void runtime_botsync_receive(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t magic;
    if (len < 10) return;
    memcpy(&magic, p, 4);
    const int from_host = p[8], n = p[9];
    if (magic != PACKET_MAGIC || n > MAX_CARS || len < 10 + (size_t)n * sizeof(entry)) return;
    const entry *e = (const entry *)(p + 10);
    pthread_mutex_lock(&lock);
    for (int k = 0; k < n; ++k) {
        char name[NAME_LEN], owner[NAME_LEN];
        memcpy(name, e[k].name, NAME_LEN); name[NAME_LEN - 1] = 0;
        memcpy(owner, e[k].owner, NAME_LEN); owner[NAME_LEN - 1] = 0;
        if (from_host)
            for (int i = 0; i < ref_count; ++i)
                if (refs[i].bot && !strcmp(refs[i].name, name)) snprintf(owners[i], NAME_LEN, "%s", owner);
        if (!e[k].has_state || !strcmp(owner, local_name)) continue; /* ours: we work it out ourselves */
        int slot = -1;
        for (int i = 0; i < pending_count; ++i)
            if (!strcmp(pending[i].name, name)) slot = i;
        if (slot < 0 && pending_count < MAX_CARS) slot = pending_count++;
        if (slot < 0) continue;
        pending[slot] = e[k];
        memcpy(pending[slot].name, name, NAME_LEN);
        pending_fresh[slot] = 1;
        pending_ms[slot] = now_ms();
    }
    pthread_mutex_unlock(&lock);
}

/* Smoothing. Between two states the game's own AI drives the bot on, so putting each state on it as it comes
 * makes it jump. Instead, every frame the bot is pulled towards where the newest state says it is now (that
 * state carried on by its velocity for its age): a part of the way for its centre of mass and rotation, and its
 * velocity set to the state's plus what closes the rest of the gap, so the physics drives it there itself. Far
 * off (a reset to the track, a big crash) it is put there at once. */
#define SNAP_M 4.0f
#define PULL_PER_S 4.0f  /* of the gap per second, put on directly */
#define CLOSE_PER_S 4.0f /* velocity added per metre of gap */

static void quat_slerp(const float *a, const float *b, float t, float *out) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float bb[4] = {b[0], b[1], b[2], b[3]};
    if (d < 0) { d = -d; for (int i = 0; i < 4; ++i) bb[i] = -bb[i]; }
    float wa = 1 - t, wb = t;
    if (d < 0.9995f) {
        const float th = acosf(d), s = sinf(th);
        wa = sinf((1 - t) * th) / s;
        wb = sinf(t * th) / s;
    }
    float n = 0;
    for (int i = 0; i < 4; ++i) { out[i] = wa * a[i] + wb * bb[i]; n += out[i] * out[i]; }
    n = sqrtf(n);
    for (int i = 0; i < 4; ++i) out[i] /= n;
}

/* The rotation's columns (hkRotation, as at +0x170/+0x180/+0x190) of quaternion q (x, y, z, w). */
static void quat_columns(const float *q, float c[3][3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    c[0][0] = 1 - 2 * (y * y + z * z); c[0][1] = 2 * (x * y + z * w);     c[0][2] = 2 * (x * z - y * w);
    c[1][0] = 2 * (x * y - z * w);     c[1][1] = 1 - 2 * (x * x + z * z); c[1][2] = 2 * (y * z + x * w);
    c[2][0] = 2 * (x * z + y * w);     c[2][1] = 2 * (y * z - x * w);     c[2][2] = 1 - 2 * (x * x + y * y);
}

/* Pulls one body towards state f (read_body's layout) aged age_s seconds, dt_s after the last pull. */
static void steer_body(uint64_t body, const float *f, float age_s, float dt_s) {
    const float pull = PULL_PER_S * dt_s;
    float *b = (float *)body;
    const float *t_com = f + 15, *t_q = f + 22, *t_lin = f + 26, *t_ang = f + 29;
    float target[3], gap[3], d2 = 0;
    for (int i = 0; i < 3; ++i) {
        target[i] = t_com[i] + t_lin[i] * age_s;
        gap[i] = target[i] - b[0x1c0 / 4 + i];
        d2 += gap[i] * gap[i];
    }
    if (d2 > SNAP_M * SNAP_M) { /* far off: put it there */
        write_body(body, f);
        for (int i = 0; i < 3; ++i) { b[0x1a0 / 4 + i] += t_lin[i] * age_s; b[0x1b0 / 4 + i] = b[0x1c0 / 4 + i] = target[i]; }
        return;
    }
    float com[3], q[4], c[3][3];
    for (int i = 0; i < 3; ++i) com[i] = b[0x1c0 / 4 + i] + gap[i] * pull;
    quat_slerp(b + 0x1e0 / 4, t_q, pull * 2, q);
    quat_columns(q, c);
    const float *local = b + 0x1f0 / 4; /* centre of mass in the body's frame */
    for (int i = 0; i < 3; ++i) {
        b[0x170 / 4 + i] = c[0][i]; b[0x180 / 4 + i] = c[1][i]; b[0x190 / 4 + i] = c[2][i];
        b[0x1a0 / 4 + i] = com[i] - (c[0][i] * local[0] + c[1][i] * local[1] + c[2][i] * local[2]);
        b[0x1b0 / 4 + i] = b[0x1c0 / 4 + i] = com[i];
        b[0x230 / 4 + i] = t_lin[i] + gap[i] * (1 - pull) * CLOSE_PER_S;
        b[0x240 / 4 + i] = t_ang[i];
    }
    memcpy(b + 0x1d0 / 4, q, 16);
    memcpy(b + 0x1e0 / 4, q, 16);
}

/* BB_BOTSYNC_STATS=1 (diagnostics): every 5 s, how far the bots' first bodies moved off their straight path
 * from one frame to the next (mean and max, cm), so the smoothing can be compared with the host's own bots. */
static void jerk_stats(float dt_s) {
    static int on = -1;
    static float prev[MAX_CARS][6], sum, worst;
    static int have[MAX_CARS], n;
    static uint64_t since;
    if (on < 0) on = BB_DIAGNOSTIC_ENV("BB_BOTSYNC_STATS") != NULL;
    if (!on) return;
    for (int i = 0; i < ref_count; ++i) {
        uint64_t body[2];
        if (!refs[i].bot || !vehicle_bodies(refs[i].vehicle, body)) { have[i] = 0; continue; }
        const float *b = (const float *)body[0];
        float now[6] = {b[0x1c0 / 4], b[0x1c0 / 4 + 1], b[0x1c0 / 4 + 2], b[0x230 / 4], b[0x230 / 4 + 1], b[0x230 / 4 + 2]};
        if (have[i] && memcmp(now, prev[i], 12)) {
            float d2 = 0;
            for (int k = 0; k < 3; ++k) {
                const float e = now[k] - (prev[i][k] + prev[i][3 + k] * dt_s);
                d2 += e * e;
            }
            const float d = sqrtf(d2);
            sum += d; n++;
            if (d > worst) worst = d;
        }
        memcpy(prev[i], now, sizeof now);
        have[i] = 1;
    }
    const uint64_t t = now_ms();
    if (!since) since = t;
    if (t - since >= 5000 && n) {
        printf("Bot sync stats: off path mean %.1f cm, max %.1f cm (%d samples)\n", sum / n * 100, worst * 100, n);
        fflush(stdout);
        sum = worst = 0; n = 0; since = t;
    }
}

/* Called on the game's thread with every pad read (the lobby manager's state: 0 lobby, else in an event). */
void runtime_botsync_frame(int lobby_state, int is_host) {
    static int started, logged;
    (void)is_host;
    if (!started) {
        pthread_t t;
        started = pthread_create(&t, NULL, scanner, NULL) == 0;
    }
    __atomic_store_n(&racing, lobby_state != 0, __ATOMIC_RELEASE);
    if (lobby_state == 0) return;
    const uint64_t now = now_ms();
    static uint64_t last;
    const uint64_t dt = last ? now - last : 16;
    last = now;
    if (dt == 0) return; /* more than one pad read this millisecond */
    const float dt_s = (float)(dt > 50 ? 50 : dt) / 1000.0f;
    pthread_mutex_lock(&lock);
    jerk_stats(dt_s);
    for (int k = 0; k < pending_count; ++k) {
        const uint64_t age = now - pending_ms[k];
        if (age > 1000) continue; /* the one in charge stopped sending: leave it to the AI */
        const float age_s = (float)(age > 250 ? 250 : age) / 1000.0f;
        for (int i = 0; i < ref_count; ++i) {
            uint64_t body[2];
            if (!refs[i].bot || strcmp(pending[k].name, refs[i].name) || !strcmp(owners[i], local_name) ||
                !vehicle_bodies(refs[i].vehicle, body))
                continue;
            steer_body(body[0], pending[k].body[0], age_s, dt_s);
            steer_body(body[1], pending[k].body[1], age_s, dt_s);
            if (pending_fresh[k] && logged++ < 4) {
                printf("Bot sync: %s steered by %s's states\n", refs[i].name, pending[k].owner);
                fflush(stdout);
            }
        }
        pending_fresh[k] = 0;
    }
    pthread_mutex_unlock(&lock);
}