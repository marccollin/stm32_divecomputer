/*
SD-card storage of tissue pressures, gradient-factor anchor and UTC timestamp.
*/
#ifndef TISSUESTORE_H
#define TISSUESTORE_H

#include <time.h>

#include "buhlmannModel.h"

struct DiveSessionState {
    bool active = false;
    bool log_ready = false;
    time_t start_time = 0;
    uint32_t duration_ms = 0;
    float max_depth_m = 0.0f;
    float depth_integral_m_s = 0.0f;
    bool surface_pending = false;
    time_t surface_time = 0;
    uint32_t surface_ms = 0;
    float surface_integral_m_s = 0.0f;
};

/*
Persists tissue pressures, first-stop anchor and UTC epoch time on the SD card.
Mounts lazily; filesystem failures are reported as false.
*/
class TissueStore {
public:
    /*
    Creates an unmounted store; no SD access occurs until load or save.
    */
    TissueStore() {}
    /*
    Stores tissue pressures t and gf_anchor in bar with UTC epoch timestamp in seconds.
    Writes tissue.tmp, closes it, removes the old tissue.txt and renames the temporary file.
    Returns true on success; replacement has an unlink-to-rename power-loss window.
    */
    bool save(const TissueState &t, float gf_anchor, time_t timestamp, bool missed_deco,
              const DiveSessionState &session);
    /*
    Loads pressures t and gf_anchor in bar and UTC epoch timestamp in seconds.
    Returns false for unavailable or invalid data, leaving outputs unchanged.
    */
    //Restores the current/last dive's latched violation; legacy records have no recorded violation.
    bool load(TissueState &t, float &gf_anchor, time_t &timestamp, bool &missed_deco,
              DiveSessionState &session);
    /*
    Loads the last saved UTC epoch timestamp in seconds into timestamp.
    Returns false when the full saved tissue record cannot be loaded.
    */
    bool saved_time(time_t &timestamp);
};

extern TissueStore tissueStore;

#endif
