/*
Shared data view and 128x64 dive/surface display renderer.
Used by live MainPage and fixed-data DemoPage without duplicating layout.
*/
#ifndef DIVESCREEN_H
#define DIVESCREEN_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <u8g.h>

#include "gasMix.h"

/*
Shared display snapshot populated by live MainPage or fixed-data DemoPage.
All inputs are metric; DiveScreen converts only their displayed representation.
*/
struct DiveView {
    bool in_dive; //True for dive layout, false for surface layout.
    float pressure_bar; //Absolute ambient pressure, bar, positive.
    float depth_m; //Current depth, metres, nonnegative.
    float max_depth_m; //Maximum dive depth, metres, nonnegative.
    uint32_t dive_time_s; //Tracked dive duration, seconds, nonnegative.
    int ndl_min; //No-decompression time, minutes, 0..99.
    float ceiling_m; //Ceiling depth, metres, nonnegative.
    bool has_stop; //True when stop fields and TTS describe a required stop.
    int stop_depth_m; //First stop depth, metres, nonnegative; used when has_stop.
    int stop_min; //First stop duration, minutes, 0..255; used when has_stop.
    int tts_min; //Total ascent and stop duration, minutes, nonnegative.
    GasMix gas; //Displayed gas fractions, 0..1 with o2 + he <= 1.
    float temperature_c; //Water temperature, degrees Celsius.
    bool clock_set; //True if the RTC time has been confirmed by the user.
    time_t now; //UTC epoch timestamp, seconds; no timezone conversion.
    bool missed_deco; //Latched ceiling violation for the current/last dive.
};

/*
Shared four-line 128x64 dive/surface formatter and renderer.
Formatting copies values to text buffers; drawing does not retain the source view.
*/
class DiveScreen {
public:
    /*
    Initialises empty display lines and no tag.
    */
    DiveScreen() : line1(), line2(), line3(), line4(), tag() {}
    /*
    Formats metric view into bounded text buffers using configured display units.
    Selects surface/dive and NDL/deco layouts; surface clock displays UTC.
    */
    void format(const DiveView &view);
    /*
    Copies text into the bounded bottom-right inverted tag, e.g. DEMO 1/3.
    Nullptr or empty text hides it.
    */
    void set_tag(const char *text);
    /*
    Draws formatted lines and optional tag on the supplied 128x64 u8g display.
    Uses grey indices 0..3, with 0 dark and 3 bright.
    */
    void draw(u8g_t *u8g) const;
private:
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[32];
    char tag[24];
};

#endif
