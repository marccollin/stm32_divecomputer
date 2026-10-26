/*
Pressure-sample integration, dive detection and tissue persistence.
Connects gas selection and decompression settings to the tissue model.
*/
#include "configComputer.h"
#include "diveController.h"
#include <diag/Trace.h>

namespace {
const float DIVE_START_M = 1.2f; //Dive starts strictly deeper than 1.2 metres.
const float DIVE_END_M = 0.8f; //Dive ends after 60 seconds continuously shallower than 0.8 metres.
}

DiveController::DiveController(BuhlmannModel &model, TissueStore &store)
    : model_(model), store_(store), gases_(), settings_(), plan_(),
      have_last_(false), last_p_(0.0f), last_tick_(0), last_save_(0),
      in_dive_(false), depth_m_(0.0f), max_depth_m_(0.0f), dive_ms_(0), shallow_ms_(0),
      dive_start_time_(0), depth_integral_m_s_(0.0f),
      dive_log_ready_(false),
      ndl_min_(BuhlmannModel::NDL_MAX), ceiling_m_(0.0f)
{
    plan_.count = 0;
    plan_.tts_min = 0;
}

//Restores saved loading and integrates elapsed surface time on air before resuming samples.
void DiveController::begin(float p_surface, time_t now)
{
    //Every 10m of water, add 1 bar, so 10.0f converts bar per 10 metres into bar per metre
    /*pressure = density × gravity × depth
    fresh: 1000 kg/m³ × 9.81 × 10 m = 98 100 Pa ≈ 0.98 bar
    salt:  1025 kg/m³ × 9.81 × 10 m = 100 550 Pa ≈ 1.006 b
    */
    settings_.bar_per_m = configComputer.get_hydrostatic_pressure() / 10.0f;
    model_.reset_to_surface(p_surface);

    TissueState saved;
    float anchor;
    time_t saved_at;
    if (!store_.load(saved, anchor, saved_at)) return;

    const double elapsed = difftime(now, saved_at);
    if (elapsed >= MAX_OFFGAS_S) return; //Treats records at least 14 days old as surface-saturated.
    model_.tissues = saved;
    model_.set_gf_anchor(anchor);
    //Unknown or negative elapsed time (clock not kept): keep tissues as saved, conservative.
    //For positive elapsed time under 14 days, off-gasses on air at surface pressure.
    if (elapsed > 0) model_.update(p_surface, p_surface, (float) elapsed, GAS_AIR);
    //A saved anchor indicates a potentially serious restart during an unfinished dive.
    in_dive_ = anchor > 0.0f;
}

//Starts a fresh dive interval without discarding residual tissue loading.
void DiveController::start_dive(time_t now)
{
    in_dive_ = true;
    dive_ms_ = 0;
    shallow_ms_ = 0;
    max_depth_m_ = 0.0f;
    dive_start_time_ = now;
    depth_integral_m_s_ = 0.0f;
    dive_log_ready_ = true;
    model_.reset_anchor();
}

//Clears the GF anchor, restores bottom-gas selection and immediately persists tissues.
void DiveController::end_dive(time_t now)
{
    in_dive_ = false;
    model_.reset_anchor();
    gases_.set_active(0);
    if (dive_log_ready_) {
        const float duration_s = dive_ms_ / 1000.0f;
        const DiveLogRecord record = {
            dive_start_time_, now, dive_ms_ / 1000, max_depth_m_,
            duration_s > 0.0f ? depth_integral_m_s_ / duration_s : 0.0f
        };
        if (!diveLog.append(record)) trace_printf("Dive log write failed.\n");
    } else {
        trace_printf("Restarted dive is not logged; start data is unavailable.\n");
    }
    dive_log_ready_ = false;
    save(last_tick_, now);
}

//Records the save tick and attempts SD persistence; failure is not propagated.
void DiveController::save(uint32_t tick_ms, time_t now)
{
    last_save_ = tick_ms;
    store_.save(model_.tissues, model_.gf_anchor(), now);
}

//Integrates between pressure samples using unsigned tick differences, then refreshes dive metrics.
void DiveController::sample(float p_amb, uint32_t tick_ms, time_t now)
{
    if (!have_last_) {
        have_last_ = true;
        last_save_ = tick_ms;
    } else {
        const uint32_t dt_ms = tick_ms - last_tick_;
        model_.update(last_p_, p_amb, dt_ms / 1000.0f, in_dive_ ? gases_.active() : GAS_AIR);

        if (in_dive_) {
            dive_ms_ += dt_ms;
            depth_integral_m_s_ += depth_m_ * (dt_ms / 1000.0f);
            //Accumulates time using the preceding sample depth; deeper samples reset the shallow timer.
            shallow_ms_ = depth_m_ < DIVE_END_M ? shallow_ms_ + dt_ms : 0;
        }
    }
    last_p_ = p_amb;
    last_tick_ = tick_ms;

    depth_m_ = (p_amb - model_.surface_pressure()) / settings_.bar_per_m;
    if (depth_m_ < 0.0f) depth_m_ = 0.0f;

    //Starts only beyond 1.2 m; hysteresis ends the dive after 60 s below 0.8 m.
    if (!in_dive_ && depth_m_ > DIVE_START_M) start_dive(now);
    if (in_dive_) {
        if (depth_m_ > max_depth_m_) max_depth_m_ = depth_m_;
        if (shallow_ms_ >= DIVE_END_MS) {
            end_dive(now);
        } else {
            model_.track_anchor(settings_);
        }
    }

    const GasMix &gas = in_dive_ ? gases_.active() : GAS_AIR;
    ndl_min_ = model_.ndl_min(p_amb, gas, settings_);
    ceiling_m_ = model_.ceiling_m(settings_);
    if (ndl_min_ == 0) {
        model_.plan(p_amb, gases_, in_dive_ ? gases_.active_index() : 0, settings_, plan_);
    } else {
        plan_.count = 0;
        plan_.tts_min = 0;
    }

    //Saves every 60 seconds, also allowing unsigned HAL tick wraparound.
    if (tick_ms - last_save_ >= SAVE_PERIOD_MS) save(tick_ms, now);
}
