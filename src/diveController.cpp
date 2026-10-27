/*
Pressure-sample integration, dive detection and tissue persistence.
Connects gas selection and decompression settings to the tissue model.
*/
#include "configComputer.h"
#include "diveController.h"
#include <diag/Trace.h>

namespace {
const float DIVE_START_M = 1.2f; //Dive starts strictly deeper than 1.2 metres.
const float DIVE_END_M = 0.8f; //Begins the surface merge window.
}

DiveController::DiveController(BuhlmannModel &model, TissueStore &store)
    : model_(model), store_(store), gases_(), settings_(), plan_(),
      have_last_(false), last_p_(0.0f), last_tick_(0), last_save_(0),
      in_dive_(false), depth_m_(0.0f), max_depth_m_(0.0f), dive_ms_(0), shallow_ms_(0),
      surface_pending_(false), surface_time_(0), surface_integral_m_s_(0.0f),
      dive_start_time_(0), depth_integral_m_s_(0.0f),
      dive_log_ready_(false), missed_deco_(false),
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
    DiveSessionState session;
    if (!store_.load(saved, anchor, saved_at, missed_deco_, session)) return;

    const double elapsed = difftime(now, saved_at);
    if (elapsed < MAX_OFFGAS_S) {
        model_.tissues = saved;
        model_.set_gf_anchor(anchor);
        //Unknown or negative elapsed time keeps saved loading.
        if (elapsed > 0) model_.update(p_surface, p_surface, (float) elapsed, GAS_AIR);
    }
    //A saved anchor indicates a potentially serious restart during an unfinished dive.
    in_dive_ = anchor > 0.0f && elapsed < MAX_OFFGAS_S;
    if (session.active) {
        in_dive_ = true;
        dive_log_ready_ = session.log_ready;
        dive_start_time_ = session.start_time;
        dive_ms_ = session.duration_ms;
        max_depth_m_ = session.max_depth_m;
        depth_integral_m_s_ = session.depth_integral_m_s;
        surface_pending_ = session.surface_pending;
        surface_time_ = session.surface_time;
        shallow_ms_ = session.surface_ms;
        surface_integral_m_s_ = session.surface_integral_m_s;
        //Only a saved surface interval can advance the merge timer while powered off.
        if (surface_pending_ && elapsed > 0) {
            const uint32_t remaining = DIVE_END_MS - shallow_ms_;
            const uint32_t dt = elapsed * 1000.0 >= remaining
                ? remaining : (uint32_t) (elapsed * 1000.0);
            shallow_ms_ += dt;
            dive_ms_ += dt;
            if (shallow_ms_ >= DIVE_END_MS) end_dive(now);
        }
    }
}

//Starts a fresh dive interval without discarding residual tissue loading.
void DiveController::start_dive(time_t now)
{
    in_dive_ = true;
    dive_ms_ = 0;
    shallow_ms_ = 0;
    surface_pending_ = false;
    surface_time_ = 0;
    surface_integral_m_s_ = 0.0f;
    max_depth_m_ = 0.0f;
    dive_start_time_ = now;
    depth_integral_m_s_ = 0.0f;
    dive_log_ready_ = true;
    missed_deco_ = false;
    model_.reset_anchor();
    save(last_tick_, now);
}

//Clears the GF anchor, restores bottom-gas selection and immediately persists tissues.
void DiveController::end_dive(time_t now)
{
    in_dive_ = false;
    model_.reset_anchor();
    gases_.set_active(0);
    if (dive_log_ready_) {
        if (surface_pending_) {
            dive_ms_ -= shallow_ms_;
            depth_integral_m_s_ = surface_integral_m_s_;
        }
        const float duration_s = dive_ms_ / 1000.0f;
        const DiveLogRecord record = {
            dive_start_time_, surface_pending_ ? surface_time_ : now,
            dive_ms_ / 1000, max_depth_m_,
            duration_s > 0.0f ? depth_integral_m_s_ / duration_s : 0.0f,
            missed_deco_
        };
        if (!diveLog.append(record)) trace_printf("Dive log write failed.\n");
    } else {
        trace_printf("Restarted dive is not logged; start data is unavailable.\n");
    }
    dive_log_ready_ = false;
    surface_pending_ = false;
    shallow_ms_ = 0;
    save(last_tick_, now);
}

//Records successful persistence; failures remain eligible for periodic retry.
void DiveController::save(uint32_t tick_ms, time_t now)
{
    DiveSessionState session;
    session.active = in_dive_;
    session.log_ready = dive_log_ready_;
    session.start_time = dive_start_time_;
    session.duration_ms = dive_ms_;
    session.max_depth_m = max_depth_m_;
    session.depth_integral_m_s = depth_integral_m_s_;
    session.surface_pending = surface_pending_;
    session.surface_time = surface_time_;
    session.surface_ms = shallow_ms_;
    session.surface_integral_m_s = surface_integral_m_s_;
    if (store_.save(model_.tissues, model_.gf_anchor(), now, missed_deco_, session)) {
        last_save_ = tick_ms;
    } else {
        trace_printf("Tissue/violation state write failed.\n");
    }
}

//Integrates between pressure samples using unsigned tick differences, then refreshes dive metrics.
void DiveController::sample(float p_amb, uint32_t tick_ms, time_t now)
{
    if (!have_last_) {
        have_last_ = true;
        last_save_ = tick_ms;
    } else {
        const uint32_t dt_ms = tick_ms - last_tick_;
        model_.update(last_p_, p_amb, dt_ms / 1000.0f,
                      in_dive_ && !surface_pending_ ? gases_.active() : GAS_AIR);

        if (in_dive_) {
            dive_ms_ += dt_ms;
            depth_integral_m_s_ += depth_m_ * (dt_ms / 1000.0f);
            if (surface_pending_) shallow_ms_ += dt_ms;
        }
    }
    last_p_ = p_amb;
    last_tick_ = tick_ms;

    depth_m_ = (p_amb - model_.surface_pressure()) / settings_.bar_per_m;
    if (depth_m_ < 0.0f) depth_m_ = 0.0f;

    //Expire the old interval before considering a descent at or after five minutes.
    if (in_dive_ && surface_pending_ && shallow_ms_ >= DIVE_END_MS) end_dive(now);
    if (!in_dive_ && depth_m_ > DIVE_START_M) start_dive(now);
    if (in_dive_) {
        if (surface_pending_ && depth_m_ > DIVE_START_M) {
            surface_pending_ = false;
            shallow_ms_ = 0;
            save(tick_ms, now);
        } else if (!surface_pending_ && depth_m_ < DIVE_END_M) {
            surface_pending_ = true;
            surface_time_ = now;
            shallow_ms_ = 0;
            surface_integral_m_s_ = depth_integral_m_s_;
            save(tick_ms, now);
        }
        if (depth_m_ > max_depth_m_) max_depth_m_ = depth_m_;
        model_.track_anchor(settings_);
        //Check before end_dive clears the anchor, including the first surface sample.
        if (!missed_deco_ && model_.ceiling_violated(p_amb, settings_)) {
            missed_deco_ = true;
            trace_printf("Missed decompression: ceiling violated.\n");
            save(tick_ms, now);
        }
    }

    const GasMix &gas = in_dive_ && !surface_pending_ ? gases_.active() : GAS_AIR;
    ndl_min_ = model_.ndl_min(p_amb, gas, settings_);
    ceiling_m_ = model_.ceiling_m(settings_);
    if (ndl_min_ == 0) {
        model_.plan(p_amb, gases_, in_dive_ && !surface_pending_ ? gases_.active_index() : 0,
                    settings_, plan_);
    } else {
        plan_.count = 0;
        plan_.tts_min = 0;
    }

    //Saves every 60 seconds, also allowing unsigned HAL tick wraparound.
    if (tick_ms - last_save_ >= SAVE_PERIOD_MS) save(tick_ms, now);
}
