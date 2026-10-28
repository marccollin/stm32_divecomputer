#include <assert.h>
#include <math.h>
#include <string.h>
#include <stm32f4xx_hal.h>

#include "sdcard.h"
#include "diskio.h"
#include "diveLog.h"
#include "tissueStore.h"

//RAM-backed FAT16 volume exercises the real FatFs implementation without SD hardware.
static const DWORD SECTORS = 8192;
static BYTE disk[SECTORS * 512];
static FATFS filesystem;

static void put_word(unsigned offset, WORD value)
{
    disk[offset] = (BYTE) value;
    disk[offset + 1] = (BYTE) (value >> 8);
}

static void format_disk()
{
    disk[0] = 0xeb;
    disk[1] = 0x3c;
    disk[2] = 0x90;
    memcpy(disk + 3, "MSDOS5.0", 8);
    put_word(11, 512);
    disk[13] = 1; //Sectors per cluster.
    put_word(14, 1); //Reserved sectors.
    disk[16] = 2; //FAT copies.
    put_word(17, 512); //Root directory entries.
    put_word(19, SECTORS);
    disk[21] = 0xf8;
    put_word(22, 32); //Sectors per FAT.
    memcpy(disk + 54, "FAT16   ", 8);
    put_word(510, 0xaa55);
    for (unsigned sector = 1; sector <= 33; sector += 32) {
        put_word(sector * 512, 0xfff8);
        put_word(sector * 512 + 2, 0xffff);
    }
}

extern "C" {
DSTATUS disk_initialize(BYTE drive) { return drive == 0 ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE drive) { return drive == 0 ? 0 : STA_NOINIT; }
DRESULT disk_read(BYTE drive, BYTE *buf, DWORD sector, UINT count)
{
    if (drive != 0 || !count || sector >= SECTORS || count > SECTORS - sector) return RES_PARERR;
    memcpy(buf, disk + sector * 512, (size_t) count * 512);
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *buf, DWORD sector, UINT count)
{
    if (drive != 0 || !count || sector >= SECTORS || count > SECTORS - sector) return RES_PARERR;
    memcpy(disk + sector * 512, buf, (size_t) count * 512);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *)
{
    return drive == 0 && command == CTRL_SYNC ? RES_OK : RES_PARERR;
}
}

bool sdcard_mount() { return f_mount(&filesystem, "", 1) == FR_OK; }

int main()
{
    static_assert(_FATFS == 64180 && _FFCONF == 64180, "Requires FatFs R0.11a");
    format_disk();
    assert(sdcard_mount());

    BuhlmannModel model;
    model.update(1.013f, 4.013f, 1200, GAS_AIR);
    DiveSessionState session;
    session.active = true;
    session.log_ready = true;
    session.start_time = 100000;
    session.duration_ms = 1260000;
    session.max_depth_m = 30;
    session.depth_integral_m_s = 36000;
    session.surface_pending = true;
    session.surface_time = 101200;
    session.surface_ms = 60000;
    session.surface_integral_m_s = 36000;
    assert(tissueStore.save(model.tissues, 1.5f, 101260, true, session));
    //Exercise replacement, unlink and rename of an existing tissue file.
    assert(tissueStore.save(model.tissues, 1.5f, 101261, true, session));
    TissueState loaded;
    float anchor;
    time_t timestamp;
    bool missed;
    DiveSessionState restored;
    assert(tissueStore.load(loaded, anchor, timestamp, missed, restored));
    assert(timestamp == 101261 && missed && fabsf(anchor - 1.5f) < 0.0001f);
    assert(restored.active && restored.surface_pending && restored.surface_ms == 60000);
    for (int i = 0; i < TissueState::COUNT; ++i) {
        assert(fabsf(loaded.n2[i] - model.tissues.n2[i]) < 0.00001f);
        assert(fabsf(loaded.he[i] - model.tissues.he[i]) < 0.00001f);
    }
    assert(tissueStore.saved_time(timestamp) && timestamp == 101261);

    const DiveLogRecord record = {100000, 101200, 1200, 30, 20, true};
    assert(diveLog.append(record));
    assert(diveLog.append(record));
    int count;
    assert(diveLog.count(count) && count == 2);
    DiveLogRecord records[2];
    int loaded_count;
    assert(diveLog.get_range(0, records, 2, loaded_count) && loaded_count == 2);
    assert(records[1].missed_deco && records[1].duration_s == 1200);
    assert(diveLog.get(0, records[0]) && records[0].start_time == 100000);

    FIL file;
    const char *name = "Long filename storage test.txt";
    assert(f_open(&file, name, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_puts("FatFs R0.11a\n", &file) >= 0);
    assert(f_close(&file) == FR_OK);
    assert(f_open(&file, name, FA_READ) == FR_OK);
    char line[32];
    assert(f_gets(line, sizeof(line), &file) && strcmp(line, "FatFs R0.11a\n") == 0);
    assert(f_close(&file) == FR_OK);
    assert(f_unlink(name) == FR_OK);
}
