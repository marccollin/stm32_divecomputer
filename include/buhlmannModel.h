/*
Hardware-independent ZHL-16C tissue loading and gradient-factor decompression.
Pressures are absolute bar; internal integration uses minutes.
*/
#ifndef BUHLMANNMODEL_H_
#define BUHLMANNMODEL_H_

#include <stdint.h>

#include "decoSettings.h"
#include "gasMix.h"

/*
Inert-gas partial pressures for the 16 ZHL-16C tissue compartments, in bar.
*/
struct TissueState {
    static const int COUNT = 16; //Compartment count, fixed at 16.
    float n2[COUNT]; //Nitrogen partial pressures, bar, nonnegative.
    float he[COUNT]; //Helium partial pressures, bar, nonnegative.
};

/*
One ascent stop with its depth, duration and carried-gas index.
*/
struct DecoStop {
    uint8_t depth_m; //Stop depth, metres, 0..255.
    uint8_t minutes; //Stop duration, minutes, 0..255.
    uint8_t gas;     //GasList index, 0..count()-1, used at this stop.
};

/*
Bounded ascent-stop list and estimated total time to surface in minutes.
*/
struct DecoPlan {
    static const int MAX_STOPS = 20; //Maximum stored stops, 20.
    DecoStop stops[MAX_STOPS]; //Ordered stops; first count entries are valid.
    int count; //Stored stop count, 0..MAX_STOPS.
    int tts_min;     //Total ascent and stop time, minutes, nonnegative.
};

/*
Hardware-independent Buhlmann ZHL-16C model with gradient factors.
Pressures are absolute bar; public integration durations are seconds.
*/
class BuhlmannModel {
public:
    /*
    Initialises rate constants and tissues saturated on air at default surface pressure.
    */
    BuhlmannModel();

    /*
    Sets surface pressure p_surface in bar and saturates tissues on air.
    Clears the deepest-first-stop anchor.
    */
    void reset_to_surface(float p_surface);
    /*
    Sets surface pressure in absolute bar without changing tissue loading.
    */
    void set_surface_pressure(float p_surface) { p_surface_ = p_surface; }
    //Returns surface pressure in absolute bar.
    float surface_pressure() const { return p_surface_; }

    /*
    Integrates tissues on gas across a linear absolute-pressure change.
    Inputs p_start and p_end are bar; dt_s is seconds; nonpositive time is ignored.
    */
    void update(float p_start, float p_end, float dt_s, const GasMix &gas);

    //Returns deepest first-stop pressure in absolute bar, or zero when absent.
    float gf_anchor() const { return gf_anchor_; }
    /*
    Restores first-stop anchor p in absolute bar; zero means no anchor.
    */
    void set_gf_anchor(float p) { gf_anchor_ = p; }
    /*
    Clears the deepest-first-stop anchor for a new dive.
    */
    void reset_anchor() { gf_anchor_ = 0.0f; }
    /*
    Deepens the stored GF-low anchor using settings s; call during a dive.
    */
    void track_anchor(const DecoSettings &s);

    /*
    Returns ceiling depth in metres using settings s and interpolated GF.
    Zero means surfacing is tolerated.
    */
    float ceiling_m(const DecoSettings &s) const;
    //True when more than 1 metre above the GF-adjusted ceiling; warning tolerance only.
    bool ceiling_violated(float p_amb, const DecoSettings &s) const;
    /*
    Returns whole minutes at absolute p_amb in bar on gas before a stop is needed.
    Uses settings s; returns 0 for decompression or at most NDL_MAX (99).
    */
    int ndl_min(float p_amb, const GasMix &gas, const DecoSettings &s) const;
    /*
    Writes an ascent estimate to out from absolute p_amb in bar using settings s.
    Starts on valid current_gas index in gases and switches to richer usable gas.
    Simulates copied tissues without changing live loading; stop storage is bounded.
    */
    void plan(float p_amb, const GasList &gases, int current_gas,
              const DecoSettings &s, DecoPlan &out);

    TissueState tissues; //Live inert-gas partial pressures, bar.

    static const int NDL_MAX = 99; //NDL display cap, minutes, 99.

private:
    float p_surface_;
    float gf_anchor_;
    float k_n2_[TissueState::COUNT];   //Rate constant, inverse minutes.
    float k_he_[TissueState::COUNT]; //Rate constant, inverse minutes.
    float f1_n2_[TissueState::COUNT];  //One-minute exponential decay factor, 0..1.
    float f1_he_[TissueState::COUNT]; //One-minute exponential decay factor, 0..1.

    static float tolerated(const TissueState &t, float gf);
    static bool tolerates(const TissueState &t, float p_amb, float gf);
    float gf_at(float p, float anchor, const DecoSettings &s) const;
    float gf_low_first_stop(const TissueState &t, const DecoSettings &s) const;
    void schreiner(TissueState &t, float p_start, float p_end, float dt_min,
                   const GasMix &gas) const;
    void constant_minute(TissueState &t, float p_amb, const GasMix &gas) const;
};

#endif //BUHLMANNMODEL_H_
