/*
Pressure-sample integration, dive detection and tissue persistence.
Connects gas selection and decompression settings to the tissue model.
*/
#ifndef DIVECONTROLLER_H
#define DIVECONTROLLER_H

#include <stdint.h>
#include <time.h>

#include "buhlmannModel.h"
#include "decoSettings.h"
#include "diveLog.h"
#include "gasMix.h"
#include "tissueStore.h"

/*
Feeds pressure samples to the Buhlmann model, detects dives and persists tissues.
Depths and decompression calculations remain metric regardless of display units.
*/
class DiveController {
public:
    /*
    Binds externally owned model and store; starts with air and default settings.
    */
    DiveController(BuhlmannModel &model, TissueStore &store);

    /*
    Initialises at absolute surface pressure p_surface in bar and UTC epoch now.
    Restores saved tissues and off-gasses on air for positive elapsed time under 14 days.
    Missing or older data leaves tissues surface-saturated; nonpositive time retains loading.
    */
    void begin(float p_surface, time_t now);
    /*
    Consumes absolute ambient pressure p_amb in bar, monotonic tick_ms in ms, and UTC now.
    Updates loading, NDL and plan; starts deeper than 1.2 m and ends after 60 s below 0.8 m.
    Saves every 60 s and at dive end.
    */
    void sample(float p_amb, uint32_t tick_ms, time_t now);

    /*
    Selects gas index idx; returns false for an invalid index, without ppO2 checks.
    */
    bool switch_gas(int idx) { return gases_.set_active(idx); }
    //Returns the mutable carried-gas list.
    GasList &gases() { return gases_; }
    //Returns mutable metric decompression settings.
    DecoSettings &settings() { return settings_; }
    //Returns absolute surface pressure in bar.
    float surface_pressure() const { return model_.surface_pressure(); }

    //Returns whether automatic dive tracking is active.
    bool in_dive() const { return in_dive_; }
    //Returns current nonnegative depth in metres.
    float depth_m() const { return depth_m_; }
    //Returns maximum tracked dive depth in metres.
    float max_depth_m() const { return max_depth_m_; }
    //Returns elapsed tracked dive time in whole seconds.
    uint32_t dive_time_s() const { return dive_ms_ / 1000; }
    //Returns NDL in minutes, 0 for decompression and 99 at the cap.
    int ndl_min() const { return ndl_min_; }
    //Returns current ceiling in metres, zero when surfacing is tolerated.
    float ceiling_m() const { return ceiling_m_; }
    //Returns the current ascent plan, empty when NDL is nonzero.
    const DecoPlan &plan() const { return plan_; }

private:
    static const uint32_t SAVE_PERIOD_MS = 60000; //Periodic save interval, ms, 60 s.
    static const uint32_t DIVE_END_MS = 60000; //Continuous shallow interval to end a dive, ms, 60 s.
    static const uint32_t MAX_OFFGAS_S = 14UL * 24 * 3600; //Saved-state age limit, seconds, 14 days.

    void start_dive(time_t now);
    void end_dive(time_t now);
    void save(uint32_t tick_ms, time_t now);

    BuhlmannModel &model_;
    TissueStore &store_;
    GasList gases_;
    DecoSettings settings_;
    DecoPlan plan_;

    bool have_last_;
    float last_p_;
    uint32_t last_tick_;
    uint32_t last_save_;

    bool in_dive_;
    float depth_m_;
    float max_depth_m_;
    uint32_t dive_ms_;
    uint32_t shallow_ms_;
    time_t dive_start_time_;
    float depth_integral_m_s_;
    bool dive_log_ready_;
    int ndl_min_;
    float ceiling_m_;
};

#endif
