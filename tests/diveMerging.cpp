#include <assert.h>
#include <math.h>
#include <map>
#include <string>
#include <string.h>
#include <stm32f4xx_hal.h>

#include "configComputer.h"
#include "diveController.h"
#include "sdcard.h"

//In-memory FatFs adapter exercises the production storage and controller together.
static std::map<std::string, std::string> files;
static std::map<FIL *, std::string> opened;
ConfigComputer configComputer;
float ConfigComputer::get_hydrostatic_pressure() { return 0.98f; }
bool sdcard_mount() { return true; }

extern "C" {
FRESULT f_open(FIL *f, const TCHAR *path, BYTE mode)
{
    if (!(mode & (FA_CREATE_ALWAYS | FA_OPEN_ALWAYS)) && !files.count(path)) return FR_NO_FILE;
    if (mode & FA_CREATE_ALWAYS) files[path].clear();
    opened[f] = path;
    f->fptr = 0;
    f->fsize = files[path].size();
    f->err = 0;
    return FR_OK;
}
FRESULT f_close(FIL *f) { opened.erase(f); return FR_OK; }
FRESULT f_sync(FIL *) { return FR_OK; }
FRESULT f_lseek(FIL *f, DWORD offset) { f->fptr = offset; return FR_OK; }
FRESULT f_unlink(const TCHAR *path) { return files.erase(path) ? FR_OK : FR_NO_FILE; }
FRESULT f_rename(const TCHAR *from, const TCHAR *to)
{
    files[to] = files.at(from);
    files.erase(from);
    return FR_OK;
}
int f_puts(const TCHAR *s, FIL *f)
{
    files.at(opened.at(f)).replace(f->fptr, strlen(s), s);
    f->fptr += strlen(s);
    f->fsize = files.at(opened.at(f)).size();
    return strlen(s);
}
TCHAR *f_gets(TCHAR *buf, int size, FIL *f)
{
    const std::string &data = files.at(opened.at(f));
    if (f->fptr >= data.size()) return nullptr;
    int n = 0;
    while (n < size - 1 && f->fptr < data.size()) {
        buf[n++] = data[f->fptr++];
        if (buf[n - 1] == '\n') break;
    }
    buf[n] = '\0';
    return buf;
}
}

static void sample(DiveController &dive, uint32_t seconds, float depth)
{
    dive.sample(1.013f + depth * 0.098f, seconds * 1000, 100000 + seconds);
}

static DiveLogRecord only_record()
{
    int count = 0;
    assert(diveLog.count(count) && count == 1);
    DiveLogRecord record;
    assert(diveLog.get(0, record));
    return record;
}

static void merge_and_finish()
{
    files.clear();
    BuhlmannModel model;
    DiveController dive(model, tissueStore);
    dive.begin(1.013f, 100000);
    sample(dive, 0, 0);
    sample(dive, 1, 10);
    sample(dive, 61, 0);
    sample(dive, 121, 1.0f); //Hysteresis band does not cancel the pending window.
    sample(dive, 360, 10); //299 seconds: resume the same dive.
    assert(dive.in_dive());
    sample(dive, 420, 0);
    sample(dive, 719, 0);
    assert(dive.in_dive());
    sample(dive, 720, 0);
    assert(!dive.in_dive());
    const DiveLogRecord record = only_record();
    assert(record.start_time == 100001 && record.end_time == 100420);
    assert(record.duration_s == 419);
    assert(record.max_depth_m > 9.99f && !record.missed_deco);
    assert(fabsf(record.average_depth_m - (60 * 10 + 239 * 1 + 60 * 10) / 419.0f) < 0.02f);

    sample(dive, 721, 10);
    assert(dive.in_dive() && dive.dive_time_s() == 0);
    BuhlmannModel restored_model;
    DiveController restored(restored_model, tissueStore);
    restored.begin(1.013f, 100721);
    assert(restored.in_dive()); //No-deco dives persist even with no GF anchor.
}

static void restart_pending(int elapsed)
{
    files.clear();
    BuhlmannModel model;
    DiveController dive(model, tissueStore);
    sample(dive, 0, 0);
    sample(dive, 1, 10);
    sample(dive, 61, 0);

    BuhlmannModel restored_model;
    DiveController restored(restored_model, tissueStore);
    restored.begin(1.013f, 100061 + elapsed);
    if (elapsed >= 300) {
        assert(!restored.in_dive());
        const DiveLogRecord record = only_record();
        assert(record.end_time == 100061 && record.duration_s == 60);
    } else {
        assert(restored.in_dive());
        //A missing/backward clock must not expire the saved window.
        const uint32_t resume = elapsed > 0 ? 61 + elapsed : 61;
        sample(restored, resume, 10);
        assert(restored.in_dive());
        sample(restored, resume + 60, 0);
        sample(restored, resume + 360, 0);
        assert(!restored.in_dive());
        only_record();
    }
}

static void exact_boundary_and_violation()
{
    files.clear();
    BuhlmannModel model;
    DiveController dive(model, tissueStore);
    sample(dive, 0, 0);
    sample(dive, 1, 40);
    for (int i = 0; i < TissueState::COUNT; ++i) model.tissues.n2[i] = 4;
    sample(dive, 2, 0);
    assert(dive.missed_deco());
    sample(dive, 100, 40);
    assert(dive.missed_deco()); //A brief surface break does not clear the flag.
    sample(dive, 101, 0);
    sample(dive, 401, 10); //Exactly five minutes: close the old dive before starting another.
    const DiveLogRecord record = only_record();
    assert(record.end_time == 100101 && record.missed_deco);
    assert(dive.in_dive() && dive.dive_time_s() == 0);
}

static void restart_preserves_violation()
{
    files.clear();
    BuhlmannModel model;
    DiveController dive(model, tissueStore);
    sample(dive, 0, 0);
    sample(dive, 1, 40);
    for (int i = 0; i < TissueState::COUNT; ++i) model.tissues.n2[i] = 4;
    sample(dive, 2, 0);
    assert(dive.missed_deco());

    BuhlmannModel restored_model;
    DiveController restored(restored_model, tissueStore);
    restored.begin(1.013f, 100102);
    assert(restored.in_dive() && restored.missed_deco());
    sample(restored, 102, 40);
    assert(restored.in_dive() && restored.missed_deco());
    sample(restored, 103, 0);
    sample(restored, 403, 0);
    assert(!restored.in_dive() && only_record().missed_deco);
}

static void legacy_and_invalid_storage()
{
    files.clear();
    BuhlmannModel model;
    DiveSessionState session;
    assert(tissueStore.save(model.tissues, 0, 100000, true, session));
    const std::string current = files.at("tissue.txt");
    for (int version = 1; version <= 2; ++version) {
        std::string legacy = current.substr(0, current.find("session "));
        legacy.replace(8, 1, version == 1 ? "1" : "2");
        if (version == 1) {
            const size_t field = legacy.find("missed_deco ");
            legacy.erase(field, legacy.find('\n', field) - field + 1);
        }
        files["tissue.txt"] = legacy;
        TissueState tissues;
        float anchor;
        time_t saved_at;
        bool missed = false;
        DiveSessionState restored;
        assert(tissueStore.load(tissues, anchor, saved_at, missed, restored));
        assert(!restored.active && missed == (version == 2));
    }

    files["tissue.txt"] = current.substr(0, current.find("session "));
    TissueState tissues;
    float anchor;
    time_t saved_at;
    bool missed = false;
    assert(!tissueStore.load(tissues, anchor, saved_at, missed, session));
}

static void tick_wraparound()
{
    files.clear();
    BuhlmannModel model;
    DiveController dive(model, tissueStore);
    dive.sample(1.013f, UINT32_MAX - 2000, 100000);
    dive.sample(1.993f, UINT32_MAX - 1000, 100001);
    dive.sample(1.013f, 999, 100003);
    dive.sample(1.013f, 300998, 100302);
    assert(dive.in_dive());
    dive.sample(1.013f, 300999, 100303);
    assert(!dive.in_dive());
    assert(only_record().duration_s == 2);
}

int main()
{
    merge_and_finish();
    restart_pending(120);
    restart_pending(300);
    restart_pending(400);
    restart_pending(15 * 24 * 3600);
    restart_pending(0);
    restart_pending(-10);
    exact_boundary_and_violation();
    restart_preserves_violation();
    legacy_and_invalid_storage();
    tick_wraparound();
}
