/*
SD-card history of completed dives.
*/
#ifndef DIVELOG_H
#define DIVELOG_H

#include <stdint.h>
#include <time.h>

struct DiveLogRecord {
    time_t start_time;
    time_t end_time;
    uint32_t duration_s;
    float max_depth_m;
    float average_depth_m;
    bool missed_deco; //A required ceiling was violated; legacy entries have no recorded violation.
};

/*
Append-only completed-dive history stored on the SD card.
*/
class DiveLog {
public:
    DiveLog() {}

    //Returns the number of valid stored dives, or false on a filesystem error.
    bool count(int &dives);
    //Loads the zero-based chronological entry at index.
    bool get(int index, DiveLogRecord &record);
    //Loads up to capacity consecutive entries, starting at first.
    bool get_range(int first, DiveLogRecord *records, int capacity, int &loaded);
    //Appends one completed dive record.
    bool append(const DiveLogRecord &record);

};

extern DiveLog diveLog;

#endif
