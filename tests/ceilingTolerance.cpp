#include <assert.h>
#include <math.h>

#include "buhlmannModel.h"

static void check_tolerance(float bar_per_m, float surface, bool interpolate)
{
    BuhlmannModel model;
    model.reset_to_surface(surface);
    DecoSettings settings;
    settings.bar_per_m = bar_per_m;
    if (!interpolate) settings.gf_low = settings.gf_high;

    const float ceiling_pressure = surface + 40.0f * bar_per_m;
    const float anchor = surface + 45.0f * bar_per_m;
    model.set_gf_anchor(anchor);
    const float gf = settings.gf_high + (settings.gf_low - settings.gf_high)
        * (ceiling_pressure - surface) / (anchor - surface);
    //Load compartment 1b to an exact 40 m ceiling using its nitrogen coefficients.
    model.tissues.n2[0] = ceiling_pressure * (gf / 0.5578f + 1.0f - gf)
        + 1.1696f * gf;
    const TissueState before = model.tissues;
    const float ceiling_before = model.ceiling_m(settings);
    assert(fabsf(ceiling_before - 40.0f) < 0.2f);

    assert(!model.ceiling_violated(surface + 41.0f * bar_per_m, settings));
    assert(!model.ceiling_violated(ceiling_pressure, settings));
    assert(!model.ceiling_violated(surface + 39.5f * bar_per_m, settings));
    assert(!model.ceiling_violated(surface + 39.0f * bar_per_m, settings));
    assert(model.ceiling_violated(surface + 38.99f * bar_per_m, settings));
    assert(model.ceiling_violated(surface, settings));
    assert(model.ceiling_m(settings) == ceiling_before);
    for (int i = 0; i < TissueState::COUNT; ++i) {
        assert(model.tissues.n2[i] == before.n2[i]);
        assert(model.tissues.he[i] == before.he[i]);
    }

    model.reset_to_surface(surface);
    assert(!model.ceiling_violated(surface, settings));
}

int main()
{
    check_tolerance(0.098f, 1.013f, false);
    check_tolerance(0.1007f, 1.013f, false);
    check_tolerance(0.098f, 0.8f, true);
    check_tolerance(0.1007f, 1.013f, true);
}
