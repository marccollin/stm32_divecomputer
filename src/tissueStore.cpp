/*
SD-card storage of tissue pressures, gradient-factor anchor and UTC timestamp.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <stm32f4xx_hal.h>

#include "sdcard.h"
#include "tissueStore.h"

TissueStore tissueStore;

namespace {
const char *FILE_NAME = "tissue.txt";
const char *TMP_NAME = "tissue.tmp";
const int VERSION = 3; //Adds active-dive and pending surface-interval state.

//Returns whether FatFs accepted the complete null-terminated text line.
bool write_line(FIL &f, const char *line)
{
    return f_puts(line, &f) >= 0;
}

//Serialises a named pressure array in bar to five decimal places.
bool write_array(FIL &f, const char *key, const float *v, int n)
{
    char line[24];
    if (!write_line(f, key)) return false;
    for (int i = 0; i < n; ++i) {
        snprintf(line, sizeof(line), " %.5f", v[i]);
        if (!write_line(f, line)) return false;
    }
    return write_line(f, "\n");
}

//Parses a matching named array; rejects missing or out-of-range pressures outside 0..20 bar.
bool read_array(char *line, const char *key, float *v, int n)
{
    const size_t len = strlen(key);
    if (strncmp(line, key, len) != 0) return false;
    char *p = line + len;
    for (int i = 0; i < n; ++i) {
        char *end;
        v[i] = strtof(p, &end);
        if (end == p || v[i] < 0.0f || v[i] > 20.0f) return false;
        p = end;
    }
    return true;
}
}

/*
Stages complete data in tissue.tmp before replacing tissue.txt by rename.
This avoids a half-written final file, but unlink then rename is not fully atomic:
a power loss between those calls can leave no tissue.txt.
*/
bool TissueStore::save(const TissueState &t, float gf_anchor, time_t timestamp, bool missed_deco,
                       const DiveSessionState &session)
{
    if (!sdcard_mount()) return false;

    FIL f;
    if (f_open(&f, TMP_NAME, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        return false;
    }
    char line[256];
    snprintf(line, sizeof(line), "version %d\ntime %ld\nanchor %.4f\nmissed_deco %d\n",
             VERSION, (long) timestamp, gf_anchor, missed_deco ? 1 : 0);
    bool ok = write_line(f, line)
        && write_array(f, "n2", t.n2, TissueState::COUNT)
        && write_array(f, "he", t.he, TissueState::COUNT);
    const int length = snprintf(line, sizeof(line),
        "session %d %d %ld %lu %.5f %.5f %d %ld %lu %.5f\n",
        session.active ? 1 : 0, session.log_ready ? 1 : 0, (long) session.start_time,
        (unsigned long) session.duration_ms, session.max_depth_m, session.depth_integral_m_s,
        session.surface_pending ? 1 : 0, (long) session.surface_time,
        (unsigned long) session.surface_ms, session.surface_integral_m_s);
    ok = ok && length > 0 && (size_t) length < sizeof(line) && write_line(f, line);
    ok = (f_close(&f) == FR_OK) && ok;
    if (!ok) return false;

    f_unlink(FILE_NAME);
    return f_rename(TMP_NAME, FILE_NAME) == FR_OK;
}

//Parses into temporary state and publishes outputs only after version and array validation.
bool TissueStore::load(TissueState &t, float &gf_anchor, time_t &timestamp, bool &missed_deco,
                       DiveSessionState &session)
{
    if (!sdcard_mount()) return false;

    FIL f;
    if (f_open(&f, FILE_NAME, FA_OPEN_EXISTING | FA_READ) != FR_OK) return false;

    TissueState tmp;
    char line[256];
    int version = 0;
    long ts = 0;
    float anchor = 0.0f;
    bool have_n2 = false, have_he = false;
    int violation = -1;
    DiveSessionState saved_session;
    bool have_session = false;
    while (f_gets(line, sizeof(line), &f)) {
        if (sscanf(line, "version %d", &version) == 1) continue;
        if (sscanf(line, "time %ld", &ts) == 1) continue;
        if (sscanf(line, "missed_deco %d", &violation) == 1) continue;
        if (strncmp(line, "session ", 8) == 0) {
            int active, ready, pending;
            long start, surface;
            unsigned long duration, surface_ms;
            if (sscanf(line, "session %d %d %ld %lu %f %f %d %ld %lu %f",
                &active, &ready, &start, &duration, &saved_session.max_depth_m,
                &saved_session.depth_integral_m_s, &pending, &surface, &surface_ms,
                &saved_session.surface_integral_m_s) != 10
                || (active != 0 && active != 1) || (ready != 0 && ready != 1)
                || (pending != 0 && pending != 1) || start < 0 || surface < 0
                || duration > UINT32_MAX || surface_ms >= 300000 || surface_ms > duration
                || (pending && (!active || surface < start))
                || !isfinite(saved_session.max_depth_m) || saved_session.max_depth_m < 0
                || saved_session.max_depth_m > 1000
                || !isfinite(saved_session.depth_integral_m_s)
                || saved_session.depth_integral_m_s < 0
                || !isfinite(saved_session.surface_integral_m_s)
                || saved_session.surface_integral_m_s < 0
                || saved_session.surface_integral_m_s > saved_session.depth_integral_m_s) {
                f_close(&f);
                return false;
            }
            saved_session.active = active == 1;
            saved_session.log_ready = ready == 1;
            saved_session.start_time = (time_t) start;
            saved_session.duration_ms = (uint32_t) duration;
            saved_session.surface_pending = pending == 1;
            saved_session.surface_time = (time_t) surface;
            saved_session.surface_ms = (uint32_t) surface_ms;
            have_session = true;
            continue;
        }
        if (strncmp(line, "anchor ", 7) == 0) { anchor = strtof(line + 7, NULL); continue; }
        if (read_array(line, "n2", tmp.n2, TissueState::COUNT)) { have_n2 = true; continue; }
        if (read_array(line, "he", tmp.he, TissueState::COUNT)) { have_he = true; continue; }
    }
    f_close(&f);

    if (version < 1 || version > VERSION || !have_n2 || !have_he
        || (version >= 2 && violation != 0 && violation != 1)
        || (version == VERSION && !have_session)) return false;
    t = tmp;
    gf_anchor = anchor;
    timestamp = (time_t) ts;
    missed_deco = version >= 2 && violation == 1;
    session = saved_session;
    return true;
}

//Reuses full-record validation before returning a saved UTC timestamp.
bool TissueStore::saved_time(time_t &timestamp)
{
    TissueState t;
    float anchor;
    bool missed_deco;
    DiveSessionState session;
    return load(t, anchor, timestamp, missed_deco, session);
}
