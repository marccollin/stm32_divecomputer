/*
SD-card storage of tissue pressures, gradient-factor anchor and UTC timestamp.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stm32f4xx_hal.h>

#include "sdcard.h"
#include "tissueStore.h"

TissueStore tissueStore;

namespace {
const char *FILE_NAME = "tissue.txt";
const char *TMP_NAME = "tissue.tmp";
const int VERSION = 1; //On-disk format version, currently 1.

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
bool TissueStore::save(const TissueState &t, float gf_anchor, time_t timestamp)
{
    if (!sdcard_mount()) return false;

    FIL f;
    if (f_open(&f, TMP_NAME, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        return false;
    }
    char line[48];
    snprintf(line, sizeof(line), "version %d\ntime %ld\nanchor %.4f\n",
             VERSION, (long) timestamp, gf_anchor);
    bool ok = write_line(f, line)
        && write_array(f, "n2", t.n2, TissueState::COUNT)
        && write_array(f, "he", t.he, TissueState::COUNT);
    ok = (f_close(&f) == FR_OK) && ok;
    if (!ok) return false;

    f_unlink(FILE_NAME);
    return f_rename(TMP_NAME, FILE_NAME) == FR_OK;
}

//Parses into temporary state and publishes outputs only after version and array validation.
bool TissueStore::load(TissueState &t, float &gf_anchor, time_t &timestamp)
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
    while (f_gets(line, sizeof(line), &f)) {
        if (sscanf(line, "version %d", &version) == 1) continue;
        if (sscanf(line, "time %ld", &ts) == 1) continue;
        if (strncmp(line, "anchor ", 7) == 0) { anchor = strtof(line + 7, NULL); continue; }
        if (read_array(line, "n2", tmp.n2, TissueState::COUNT)) { have_n2 = true; continue; }
        if (read_array(line, "he", tmp.he, TissueState::COUNT)) { have_he = true; continue; }
    }
    f_close(&f);

    if (version != VERSION || !have_n2 || !have_he) return false;
    t = tmp;
    gf_anchor = anchor;
    timestamp = (time_t) ts;
    return true;
}

//Reuses full-record validation before returning a saved UTC timestamp.
bool TissueStore::saved_time(time_t &timestamp)
{
    TissueState t;
    float anchor;
    return load(t, anchor, timestamp);
}
