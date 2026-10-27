/*
Hardware-independent ZHL-16C tissue loading and gradient-factor decompression.
Pressures are absolute bar; internal integration uses minutes.
*/
#include <math.h>

#include "buhlmannModel.h"
#include "constants.h"

namespace {

const int N = TissueState::COUNT; //Compartment count, 16.

/*
ZHL-16C coefficients using compartment 1b.
Half-times are minutes; a coefficients are bar; b coefficients are dimensionless.
*/
const float N2_HALF[N] = {5.0, 8.0, 12.5, 18.5, 27.0, 38.3, 54.3, 77.0,
    109.0, 146.0, 187.0, 239.0, 305.0, 390.0, 498.0, 635.0};
const float N2_A[N] = {1.1696, 1.0, 0.8618, 0.7562, 0.62, 0.5043, 0.441, 0.4,
    0.375, 0.35, 0.3295, 0.3065, 0.2835, 0.261, 0.248, 0.2327};
const float N2_B[N] = {0.5578, 0.6514, 0.7222, 0.7825, 0.8126, 0.8434, 0.8693, 0.8910,
    0.9092, 0.9222, 0.9319, 0.9403, 0.9477, 0.9544, 0.9602, 0.9653};
const float HE_HALF[N] = {1.88, 3.02, 4.72, 6.99, 10.21, 14.48, 20.53, 29.11,
    41.20, 55.19, 70.69, 90.34, 115.29, 147.42, 188.24, 240.03};
const float HE_A[N] = {1.6189, 1.383, 1.1919, 1.0458, 0.922, 0.8205, 0.7305, 0.6502,
    0.595, 0.5545, 0.5333, 0.5189, 0.5181, 0.5176, 0.5172, 0.5119};
const float HE_B[N] = {0.4770, 0.5747, 0.6527, 0.7223, 0.7582, 0.7957, 0.8279, 0.8553,
    0.8757, 0.8903, 0.8997, 0.9073, 0.9122, 0.9171, 0.9217, 0.9267};

const float LN2 = 0.69314718f; //Natural logarithm of two, dimensionless.
const float CEILING_STEP_BAR = 0.01f; //Ceiling-search pressure increment, bar.
const float CEILING_TOLERANCE_M = 1.0f; //Warning tolerance only; does not relax the ascent plan.
const int MAX_STOP_MIN = 255; //Per-stop simulation cap, minutes; fits uint8_t.
const int MAX_TOTAL_MIN = 999; //Total stop-time simulation cap, minutes.

//Returns the configured 0.0627 bar water-vapour correction to inspired pressure.
float wv() { return GlobalConstants::water_vapor_pressure; }

//Applies the Schreiner equation to one gas: pressures in bar, rate in bar/minute, t in minutes.
void schreiner_one(float &p, float palv0, float rate, float k, float t)
{
    p = palv0 + rate * (t - 1.0f / k) - (palv0 - p - rate / k) * expf(-k * t);
}

}

//Precomputes inverse-minute uptake rates and one-minute decay factors for all compartments.
BuhlmannModel::BuhlmannModel()
    : tissues(), p_surface_(GlobalConstants::initial_Ambient_Pressure), gf_anchor_(0.0f)
{
    for (int i = 0; i < N; ++i) {
        k_n2_[i] = LN2 / N2_HALF[i];
        k_he_[i] = LN2 / HE_HALF[i];
        f1_n2_[i] = expf(-k_n2_[i]);
        f1_he_[i] = expf(-k_he_[i]);
    }
    reset_to_surface(p_surface_);
}

//Saturates each nitrogen compartment at inspired air pressure and clears helium and anchor.
void BuhlmannModel::reset_to_surface(float p_surface)
{
    p_surface_ = p_surface;
    gf_anchor_ = 0.0f;
    const float pn2 = (p_surface - wv()) * GAS_AIR.n2();
    for (int i = 0; i < N; ++i) {
        tissues.n2[i] = pn2;
        tissues.he[i] = 0.0f;
    }
}

//Integrates linear inspired-pressure changes separately for nitrogen and helium.
void BuhlmannModel::schreiner(TissueState &t, float p_start, float p_end, float dt_min,
                              const GasMix &gas) const
{
    if (dt_min <= 0.0f) return;
    const float fn2 = gas.n2();
    const float fhe = gas.he;
    //Subtracts 0.0627 bar water vapour before applying inert-gas fractions.
    const float palv = p_start - wv();
    const float rate = (p_end - p_start) / dt_min;
    for (int i = 0; i < N; ++i) {
        schreiner_one(t.n2[i], palv * fn2, rate * fn2, k_n2_[i], dt_min);
        schreiner_one(t.he[i], palv * fhe, rate * fhe, k_he_[i], dt_min);
    }
}

//Uses cached exponential factors for one minute at constant ambient pressure.
void BuhlmannModel::constant_minute(TissueState &t, float p_amb, const GasMix &gas) const
{
    const float pn2 = (p_amb - wv()) * gas.n2();
    const float phe = (p_amb - wv()) * gas.he;
    for (int i = 0; i < N; ++i) {
        t.n2[i] = pn2 + (t.n2[i] - pn2) * f1_n2_[i];
        t.he[i] = phe + (t.he[i] - phe) * f1_he_[i];
    }
}

//Converts the public duration from seconds to minutes before Schreiner integration.
void BuhlmannModel::update(float p_start, float p_end, float dt_s, const GasMix &gas)
{
    schreiner(tissues, p_start, p_end, dt_s / 60.0f, gas);
}

//Returns the deepest required ambient pressure among all compartments for the given GF.
float BuhlmannModel::tolerated(const TissueState &t, float gf)
{
    float worst = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float pt = t.n2[i] + t.he[i];
        if (pt <= 0.0f) continue;
        //Weights Buhlmann a (bar) and b (dimensionless) by nitrogen and helium partial pressures.
        const float a = (N2_A[i] * t.n2[i] + HE_A[i] * t.he[i]) / pt;
        const float b = (N2_B[i] * t.n2[i] + HE_B[i] * t.he[i]) / pt;
        //GF-adjusted tolerated pressure: (Pt - a*gf)/(gf/b + 1 - gf), in absolute bar.
        const float p = (pt - a * gf) / (gf / b + 1.0f - gf);
        if (p > worst) worst = p;
    }
    return worst;
}

//Compares the worst compartment limit with ambient pressure using a small bar tolerance.
bool BuhlmannModel::tolerates(const TissueState &t, float p_amb, float gf)
{
    return tolerated(t, gf) <= p_amb + 0.0001f;
}

//Interpolates GF linearly in pressure from GF low at the deepest first stop to GF high at surface.
float BuhlmannModel::gf_at(float p, float anchor, const DecoSettings &s) const
{
    if (anchor <= p_surface_) return s.gf_high;
    if (p >= anchor) return s.gf_low;
    return s.gf_high + (s.gf_low - s.gf_high) * (p - p_surface_) / (anchor - p_surface_);
}

//Pressure of the first stop (rounded to the stop grid) using GF low, 0 if none.
float BuhlmannModel::gf_low_first_stop(const TissueState &t, const DecoSettings &s) const
{
    const float depth = (tolerated(t, s.gf_low) - p_surface_) / s.bar_per_m;
    if (depth <= 0.0f) return 0.0f;
    float stop = ceilf(depth / s.stop_step_m) * s.stop_step_m;
    if (stop < s.last_stop_m) stop = s.last_stop_m;
    return p_surface_ + stop * s.bar_per_m;
}

//Retains the deepest first-stop pressure reached; the GF anchor never shallows during a dive.
void BuhlmannModel::track_anchor(const DecoSettings &s)
{
    const float first = gf_low_first_stop(tissues, s);
    if (first > gf_anchor_) gf_anchor_ = first;
}

//Searches in 0.01 bar increments from surface until interpolated GF is tolerated.
float BuhlmannModel::ceiling_m(const DecoSettings &s) const
{
    const float limit = tolerated(tissues, s.gf_low);
    float p = p_surface_;
    while (p < limit && !tolerates(tissues, p, gf_at(p, gf_anchor_, s))) {
        p += CEILING_STEP_BAR;
    }
    const float depth = (p - p_surface_) / s.bar_per_m;
    return depth > 0.0f ? depth : 0.0f;
}

bool BuhlmannModel::ceiling_violated(float p_amb, const DecoSettings &s) const
{
    const float p_tolerated = p_amb + CEILING_TOLERANCE_M * s.bar_per_m;
    return !tolerates(tissues, p_tolerated, gf_at(p_tolerated, gf_anchor_, s));
}

//Simulates copied tissues in one-minute steps and tests surface tolerance at GF high.
int BuhlmannModel::ndl_min(float p_amb, const GasMix &gas, const DecoSettings &s) const
{
    TissueState t = tissues;
    for (int m = 0; m < NDL_MAX; ++m) {
        if (!tolerates(t, p_surface_, s.gf_high)) return m;
        constant_minute(t, p_amb, gas);
    }
    return NDL_MAX;
}

//Simulates ascent and whole-minute stops on copied tissues, with richer usable gas switches.
void BuhlmannModel::plan(float p_amb, const GasList &gases, int current_gas,
                         const DecoSettings &s, DecoPlan &out)
{
    out.count = 0;
    out.tts_min = 0;

    TissueState t = tissues;
    float travel_min = 0.0f;
    int stop_min_total = 0;
    int gas = current_gas;
    float p = p_amb;

    const float first = gf_low_first_stop(t, s);
    if (first <= 0.0f || tolerates(t, p_surface_, s.gf_high)) {
        out.tts_min = (int) ceilf((p_amb - p_surface_) / s.bar_per_m / s.ascent_rate_m_min);
        if (out.tts_min < 0) out.tts_min = 0;
        return;
    }
    //Uses the deepest first stop as the GF-low anchor, including the saved dive anchor.
    const float anchor = first > gf_anchor_ ? first : gf_anchor_;
    float stop_d = roundf((first - p_surface_) / s.bar_per_m);

    while (stop_d > 0.0f) {
        const float p_stop = p_surface_ + stop_d * s.bar_per_m;
        if (p > p_stop) {
            const float dt = (p - p_stop) / s.bar_per_m / s.ascent_rate_m_min;
            schreiner(t, p, p_stop, dt, gases.get(gas));
            travel_min += dt;
        }
        p = p_stop;

        const int best = gases.best_for(p_stop, s.max_ppo2_deco);
        if (best >= 0 && gases.get(best).o2 > gases.get(gas).o2) gas = best;

        float next_d = stop_d - s.stop_step_m;
        if (next_d < s.last_stop_m - 0.01f) next_d = 0.0f;
        const float p_next = p_surface_ + next_d * s.bar_per_m;
        const float gf_next = gf_at(p_next, anchor, s);

        int minutes = 0;
        while (!tolerates(t, p_next, gf_next) && minutes < MAX_STOP_MIN
               && stop_min_total + minutes < MAX_TOTAL_MIN) {
            constant_minute(t, p_stop, gases.get(gas));
            ++minutes;
        }
        stop_min_total += minutes;
        if (minutes > 0 && out.count < DecoPlan::MAX_STOPS) {
            DecoStop &st = out.stops[out.count++];
            st.depth_m = (uint8_t) stop_d;
            st.minutes = (uint8_t) minutes;
            st.gas = (uint8_t) gas;
        }
        stop_d = next_d;
    }
    travel_min += (p - p_surface_) / s.bar_per_m / s.ascent_rate_m_min;
    out.tts_min = (int) ceilf(travel_min) + stop_min_total;
}
