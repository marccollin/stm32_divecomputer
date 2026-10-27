/*
SD-card history of completed dives.
*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <stm32f4xx_hal.h>

#include "sdcard.h"
#include "diveLog.h"

DiveLog diveLog;

namespace {
const char *FILE_NAME = "dives.csv";

bool parse_record(const char *line, DiveLogRecord &record)
{
    long start, end;
    unsigned long duration;
    float max_depth, average_depth;
    int consumed = 0;
    if (sscanf(line, "%ld,%ld,%lu,%f,%f%n", &start, &end, &duration,
               &max_depth, &average_depth, &consumed) != 5) return false;
    int violation = 0;
    if (line[consumed] == ',') {
        int extra = 0;
        if (sscanf(line + consumed, ",%d%n", &violation, &extra) != 1
            || (violation != 0 && violation != 1)) return false;
        consumed += extra;
    }
    for (const char *p = line + consumed; *p != '\0'; ++p) {
        if (*p != '\r' && *p != '\n') return false;
    }
    if (start < 0 || end < start || duration == 0 || duration > UINT32_MAX
        || max_depth != max_depth || average_depth != average_depth
        || max_depth < 0.0f || max_depth > 1000.0f
        || average_depth < 0.0f || average_depth > max_depth) return false;

    record.start_time = (time_t) start;
    record.end_time = (time_t) end;
    record.duration_s = (uint32_t) duration;
    record.max_depth_m = max_depth;
    record.average_depth_m = average_depth;
    record.missed_deco = violation == 1;
    return true;
}

bool read_record(FIL &file, DiveLogRecord &record)
{
    char line[96];
    while (f_gets(line, sizeof(line), &file)) {
        if (parse_record(line, record)) return true;
    }
    return false;
}
}

//Counts valid records; a missing history file is an empty log.
bool DiveLog::count(int &dives)
{
    if (!sdcard_mount()) return false;

    FIL file;
    const FRESULT opened = f_open(&file, FILE_NAME, FA_OPEN_EXISTING | FA_READ);
    if (opened == FR_NO_FILE) {
        dives = 0;
        return true;
    }
    if (opened != FR_OK) return false;

    int total = 0;
    DiveLogRecord record;
    while (read_record(file, record)) {
        if (total == INT_MAX) {
            f_close(&file);
            return false;
        }
        ++total;
    }
    const bool ok = !f_error(&file);
    const FRESULT closed = f_close(&file);
    if (!ok || closed != FR_OK) return false;

    dives = total;
    return true;
}

//Loads the requested valid chronological record without allocating a history-sized buffer.
bool DiveLog::get(int index, DiveLogRecord &record)
{
    if (index < 0 || !sdcard_mount()) return false;

    FIL file;
    if (f_open(&file, FILE_NAME, FA_OPEN_EXISTING | FA_READ) != FR_OK) return false;

    DiveLogRecord current;
    int current_index = 0;
    while (read_record(file, current)) {
        if (current_index == index) {
            const FRESULT closed = f_close(&file);
            if (closed != FR_OK) return false;
            record = current;
            return true;
        }
        ++current_index;
    }
    f_close(&file);
    return false;
}

//Loads a consecutive page in one scan rather than reopening and rescanning per row.
bool DiveLog::get_range(int first, DiveLogRecord *records, int capacity, int &loaded)
{
    if (first < 0 || capacity < 0 || (capacity > 0 && records == nullptr)
        || !sdcard_mount()) return false;

    FIL file;
    const FRESULT opened = f_open(&file, FILE_NAME, FA_OPEN_EXISTING | FA_READ);
    if (opened == FR_NO_FILE) {
        loaded = 0;
        return true;
    }
    if (opened != FR_OK) return false;

    int index = 0;
    DiveLogRecord current;
    while (index < first && read_record(file, current)) ++index;

    int result_count = 0;
    while (index >= first && result_count < capacity && read_record(file, current)) {
        records[result_count++] = current;
        ++index;
    }
    const bool ok = !f_error(&file);
    const FRESULT closed = f_close(&file);
    if (!ok || closed != FR_OK) return false;

    loaded = result_count;
    return true;
}

//Appends a validated record, then flushes it before reporting success.
bool DiveLog::append(const DiveLogRecord &record)
{
    if (!sdcard_mount() || record.start_time < 0 || record.end_time < record.start_time
        || record.duration_s == 0 || record.max_depth_m < 0.0f
        || record.max_depth_m != record.max_depth_m
        || record.average_depth_m != record.average_depth_m
        || record.max_depth_m > 1000.0f
        || record.average_depth_m < 0.0f || record.average_depth_m > record.max_depth_m) {
        return false;
    }

    FIL file;
    if (f_open(&file, FILE_NAME, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK) return false;
    bool ok = f_lseek(&file, f_size(&file)) == FR_OK;

    char line[96];
    snprintf(line, sizeof(line), "%ld,%ld,%lu,%.2f,%.2f,%d\n",
             (long) record.start_time, (long) record.end_time,
             (unsigned long) record.duration_s, record.max_depth_m, record.average_depth_m,
             record.missed_deco ? 1 : 0);
    if (ok) ok = f_puts(line, &file) >= 0;
    if (ok) ok = f_sync(&file) == FR_OK;
    const FRESULT closed = f_close(&file);
    return ok && closed == FR_OK;
}
