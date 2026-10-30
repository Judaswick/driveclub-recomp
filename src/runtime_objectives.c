/* Objectives in private lobby races, the way the tour shows them: three goals on the left of the screen during the
 * race, each turning gold when it is met.
 *
 * The game's own tour objectives cannot be used there: a lobby race given a tour event's id and hasObjectives still
 * shows and evaluates none (the objective setup does not run in lobby races). So the goals are picked and watched
 * here, from what the runtime can read:
 *   - the position: the HUD's "POS 1/2" text (probe.c hooks the widget's "%d/%d" format, runtime_objectives_position),
 *     set only when the place changes;
 *   - the HUD itself: its race time is set every frame while it shows (runtime_objectives_hud); when it stops, the
 *     race is over (the HUD goes at the finish);
 *   - the speed of the player's car and its top speed (runtime_player_car in runtime_nitro.c).
 * The goals stay realistic: the finish place follows the size of the field, the speed goal the car's top speed and
 * what was reached on this track before (kept per track in <user>/objectives.ini), the lead time the race.
 *
 * A race is a lobby race when its config says kNoFaceOffs (runtime_race_config_hook in runtime_faceoffs.c). */
#include "runtime.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SPEED_RATIO_FIRST 0.70f /* of the car's top speed, on a track never driven before */
#define SPEED_RATIO_MAX 0.95f
#define LEAD_SECONDS 15.0f
#define END_AFTER_US 3000000   /* no race time on the HUD for this long: the race is over */
#define SUMMARY_US 10000000    /* the result stays this long after the race */
#define SPEED_WAIT_US 15000000 /* the car's top speed not readable for this long: a plain goal */
#define RESUME_US 45000000    /* the HUD back within this long (the pause menu): the race went on */
#define TRACKS_MAX 256

enum { GOAL_FINISH, GOAL_SPEED, GOAL_LEAD, GOALS };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char track[64];
static int pending;   /* a lobby race's config was built: its goals start with its first position */
static int goals_on;  /* the goals are shown (the switch at the race's start) */
static int result_seq, result_place, result_field; /* the last finished race, for the standings */
static int active;    /* the race runs */
static uint64_t last_hud_us, ended_us, started_us;
static int position, field;
static int finish_place;           /* the finish goal: this place or better */
static float speed_goal, best_speed; /* m/s */
static float lead_time, lead_goal;
static int met[GOALS], failed[GOALS];
static uint64_t last_frame_us;

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

/* objectives.ini: one "track=ratio" per line, the best share of a car's top speed reached on the track */
static void ini_path(char *path, size_t size) {
    const char *user = runtime_file_user_dir();
    snprintf(path, size, "%s/objectives.ini", user && *user ? user : ".");
}

static float ratio_of(const char *name) {
    char path[600], line[128];
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    float r = 0.0f;
    if (!f) return 0.0f;
    const size_t n = strlen(name);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, name, n) && line[n] == '=') r = (float)atof(line + n + 1);
    fclose(f);
    return r;
}

static void ratio_store(const char *name, float ratio) {
    char path[600], lines[TRACKS_MAX][128];
    int count = 0, found = 0;
    ini_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (f) {
        while (count < TRACKS_MAX && fgets(lines[count], sizeof lines[0], f)) {
            const size_t n = strlen(name);
            if (!strncmp(lines[count], name, n) && lines[count][n] == '=') {
                snprintf(lines[count], sizeof lines[0], "%s=%.3f\n", name, ratio);
                found = 1;
            }
            ++count;
        }
        fclose(f);
    }
    if (!found && count < TRACKS_MAX) snprintf(lines[count++], sizeof lines[0], "%s=%.3f\n", name, ratio);
    f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < count; ++i) fputs(lines[i], f);
    fclose(f);
}

/* From runtime_race_config_hook: a lobby race on this track is being set up. */
void runtime_objectives_race(const char *name) {
    pthread_mutex_lock(&lock);
    snprintf(track, sizeof track, "%s", name ? name : "");
    pending = 1; /* every lobby race is followed: its place goes to the championship standings */
    goals_on = runtime_objectives_enabled();
    active = 0;
    ended_us = 0;
    pthread_mutex_unlock(&lock);
}

/* The speed goal, once the car's top speed can be read (not always yet at the first position): in whole 5 km/h; still
 * unknown after SPEED_WAIT_US, a plain 150 km/h. 1 when set. */
static int set_speed_goal(uint64_t t) {
    float speed, top;
    runtime_player_car(&speed, &top);
    if (top <= 0.0f && t - started_us < SPEED_WAIT_US) return 0;
    float ratio = ratio_of(track);
    ratio = ratio > 0.0f ? fminf(ratio * 0.98f, SPEED_RATIO_MAX) : SPEED_RATIO_FIRST;
    const float kmh = top > 0.0f ? top * 3.6f * ratio : 150.0f;
    speed_goal = (floorf(kmh / 5.0f) * 5.0f) / 3.6f;
    printf("Objectives: reach %.0f km/h (top speed %.0f km/h)\n", speed_goal * 3.6f, top * 3.6f);
    fflush(stdout);
    return 1;
}

static void start(int total) {
    field = total;
    finish_place = total <= 3 ? 1 : total <= 8 ? 3 : 5;
    started_us = now_us();
    speed_goal = 0.0f;
    best_speed = 0.0f;
    lead_time = 0.0f;
    lead_goal = total > 1 ? LEAD_SECONDS : 0.0f;
    memset(met, 0, sizeof met);
    memset(failed, 0, sizeof failed);
    active = 1;
    pending = 0;
    printf("Objectives: %s, %d cars: finish %d%s, lead %.0f s\n", track, total, finish_place,
           finish_place == 1 ? "st" : " or better", lead_goal);
    fflush(stdout);
}

static void finish(uint64_t t) {
    active = 0;
    ended_us = t;
    met[GOAL_FINISH] = position >= 1 && position <= finish_place;
    ++result_seq;
    result_place = position;
    result_field = field;
    for (int i = 0; i < GOALS; ++i) failed[i] = !met[i];
    if (!lead_goal) failed[GOAL_LEAD] = 0;
    float speed, top;
    runtime_player_car(&speed, &top);
    if (top > 0.0f && best_speed > 0.0f) {
        const float reached = best_speed / top;
        if (reached > ratio_of(track)) ratio_store(track, reached);
    }
    printf("Objectives: race over, place %d of %d: finish %s, speed %s (best %.0f km/h), lead %s (%.0f s)\n", position,
           field, met[GOAL_FINISH] ? "met" : "missed", met[GOAL_SPEED] ? "met" : "missed", best_speed * 3.6f,
           met[GOAL_LEAD] ? "met" : "missed", lead_time);
    fflush(stdout);
}

/* The HUD's position text (probe.c): place and field. */
ABI void runtime_objectives_position(int place, int total) {
    if (place < 1 || total < 1 || total > 64) return;
    pthread_mutex_lock(&lock);
    if (pending && !active) start(total);
    position = place;
    if (active) field = total;
    last_hud_us = now_us();
    pthread_mutex_unlock(&lock);
}

/* The HUD's race time was set (probe.c): the race HUD is on screen. Back soon after it went (the pause menu hides it
 * too), the race goes on. */
ABI void runtime_objectives_hud(void) {
    const uint64_t t = now_us();
    pthread_mutex_lock(&lock);
    if (!active && !pending && ended_us && t - ended_us < RESUME_US) {
        active = 1;
        ended_us = 0;
        met[GOAL_FINISH] = 0;
        memset(failed, 0, sizeof failed);
        printf("Objectives: the HUD is back, the race goes on\n");
        fflush(stdout);
    }
    last_hud_us = t;
    pthread_mutex_unlock(&lock);
}

/* With every pad read: speed and lead, and the end of the race. */
void runtime_objectives_frame(void) {
    const uint64_t t = now_us();
    pthread_mutex_lock(&lock);
    const float dt = last_frame_us ? fminf((float)(t - last_frame_us) / 1e6f, 0.1f) : 0.0f;
    last_frame_us = t;
    if (active) {
        if (t - last_hud_us > END_AFTER_US) {
            finish(last_hud_us);
        } else {
            float speed, top;
            if (runtime_player_car(&speed, &top) && speed > best_speed && speed < 150.0f) best_speed = speed;
            if (!speed_goal) set_speed_goal(t);
            if (speed_goal && best_speed >= speed_goal) met[GOAL_SPEED] = 1;
            if (position == 1 && field > 1) lead_time += dt;
            if (lead_goal && lead_time >= lead_goal) met[GOAL_LEAD] = 1;
        }
    }
    pthread_mutex_unlock(&lock);
}

/* The last finished lobby race's place and field; the number of races finished so far (0: none yet). A pause long
 * enough to end a race here sends a place too early; the real end sends it again and the server keeps the last. */
int runtime_lobby_result(int *place, int *field, int *racing) {
    pthread_mutex_lock(&lock);
    const int seq = result_seq;
    *place = result_place;
    *field = result_field;
    *racing = active;
    pthread_mutex_unlock(&lock);
    return seq;
}

/* For the overlay: the goals' lines, whether each is met (1), missed (-1) or open (0), and a progress text each.
 * 0 when nothing is to be shown (no lobby race, or its summary is over). Shown also says the race is on: the HUD is
 * there. */
int runtime_objectives_view(char text[3][64], char progress[3][32], int state[3], int *over) {
    const uint64_t t = now_us();
    pthread_mutex_lock(&lock);
    const int show = goals_on && ((active && t - last_hud_us < 500000) || (ended_us && t - ended_us < SUMMARY_US));
    if (show) {
        if (finish_place == 1) snprintf(text[0], 64, "Win the race");
        else snprintf(text[0], 64, "Finish in the top %d", finish_place);
        snprintf(progress[0], 32, "P%d", position);
        if (speed_goal) snprintf(text[1], 64, "Reach %.0f km/h", speed_goal * 3.6f);
        else snprintf(text[1], 64, "Reach ... km/h");
        snprintf(progress[1], 32, "%.0f km/h", best_speed * 3.6f);
        if (lead_goal) {
            snprintf(text[2], 64, "Lead the race for %.0f s", lead_goal);
            snprintf(progress[2], 32, "%.0f / %.0f s", fminf(lead_time, lead_goal), lead_goal);
        } else {
            snprintf(text[2], 64, "Lead the race");
            progress[2][0] = 0;
        }
        for (int i = 0; i < GOALS; ++i) state[i] = met[i] ? 1 : failed[i] ? -1 : 0;
        /* the finish goal is only met at the end; during the race it shows whether the place would do */
        if (active) state[0] = 0;
        *over = !active;
    }
    pthread_mutex_unlock(&lock);
    return show;
}
