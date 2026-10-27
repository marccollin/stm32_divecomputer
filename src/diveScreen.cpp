/*
Shared data view and 128x64 dive/surface display renderer.
Used by live MainPage and fixed-data DemoPage without duplicating layout.
*/
#include <stdio.h>

#include "configComputer.h"
#include "diveScreen.h"
#include "units.h"

//Builds four bounded text lines from the shared live/demo snapshot and display-unit setting.
void DiveScreen::format(const DiveView &v)
{
    const unitSystem sys = configComputer.get_system_unit();
    char gas[12], depth[12], temp[8], value[12];
    units::format_gas(v.gas, gas, sizeof(gas));
    units::format_temperature(v.temperature_c, sys, temp, sizeof(temp));
    const uint32_t t = v.dive_time_s;

    if (v.in_dive) {
        units::format_depth(v.depth_m, sys, depth, sizeof(depth));
        snprintf(line1, sizeof(line1), "%s %lu:%02lu", depth,
                 (unsigned long) (t / 60), (unsigned long) (t % 60));
    } else {
        units::format_pressure(v.pressure_bar, sys, line1, sizeof(line1));
    }

    if (v.has_stop) {
        units::format_stop(v.stop_depth_m, sys, value, sizeof(value));
        snprintf(line2, sizeof(line2), "STOP %s %d'", value, v.stop_min);
        units::format_depth(v.ceiling_m, sys, value, sizeof(value));
        snprintf(line3, sizeof(line3), "CEIL %s TTS %d'", value, v.tts_min);
    } else {
        snprintf(line2, sizeof(line2), "NDL %d'", v.ndl_min);
        units::format_depth(v.max_depth_m, sys, value, sizeof(value));
        snprintf(line3, sizeof(line3), "MAX %s", value);
    }

    if (v.missed_deco) {
        snprintf(line4, sizeof(line4), "MISSED DECO%s", v.clock_set ? "" : "  SET TIME");
    } else if (v.in_dive) {
        snprintf(line4, sizeof(line4), "%s  %s", gas, temp);
    } else if (!v.clock_set) {
        snprintf(line4, sizeof(line4), "%s  %s  SET TIME", gas, temp);
    } else {
        struct tm tm_now;
        //Shows UTC directly, with no timezone or daylight-saving adjustment.
        gmtime_r(&v.now, &tm_now);
        snprintf(line4, sizeof(line4), "%s  %s  %02d:%02d", gas, temp, tm_now.tm_hour, tm_now.tm_min);
    }
}

void DiveScreen::set_tag(const char *text)
{
    snprintf(tag, sizeof(tag), "%s", text != nullptr ? text : "");
}

//Places four text baselines and an optional inverted tag within the 128x64 display.
void DiveScreen::draw(u8g_t *u8g) const
{
    //The u8g colour index is a grey level, 0..3: dark through brightest.
    u8g_SetColorIndex(u8g, 3);
    u8g_SetFont(u8g, u8g_font_helvB14);
    u8g_DrawStr(u8g, 2, 16, line1);

    u8g_SetFont(u8g, u8g_font_helvB10);
    u8g_DrawStr(u8g, 2, 32, line2);

    u8g_SetColorIndex(u8g, 2);
    u8g_SetFont(u8g, u8g_font_helvR08);
    u8g_DrawStr(u8g, 2, 46, line3);
    u8g_DrawStr(u8g, 2, 60, line4);

    if (tag[0] != '\0') {
        const int w = u8g_GetStrWidth(u8g, tag) + 4;
        u8g_SetColorIndex(u8g, 3);
        u8g_DrawBox(u8g, 128 - w, 51, w, 13);
        u8g_SetColorIndex(u8g, 0);
        u8g_DrawStr(u8g, 128 - w + 2, 61, tag);
    }
}
