/*
Fixed dive examples rendered by the same DiveScreen as the live MainPage.
*/
#include <stdio.h>

#include "demoPage.h"

namespace {
/*
DiveView field order: dive flag, bar, depth/max metres, time seconds, NDL minutes,
ceiling metres, stop flag, stop metres/minutes, TTS minutes, gas fractions,
Celsius, clock-confirmed flag, UTC epoch seconds and missed-decompression flag.
*/
const DiveView EXAMPLES[] = {
    //No-deco dive on air
    {true, 2.85f, 18.4f, 21.2f, 12 * 60 + 34, 21, 0.0f, false, 0, 0, 0, {0.21f, 0.0f}, 19.0f, true, 0, false},
    //Short NDL near the limit on nitrox
    {true, 4.05f, 30.6f, 31.0f, 18 * 60 + 5, 3, 0.0f, false, 0, 0, 0, {0.32f, 0.0f}, 16.0f, true, 0, false},
    //Deco required, still on bottom gas
    {true, 4.95f, 39.8f, 40.3f, 25 * 60 + 12, 0, 7.4f, true, 9, 1, 22, {0.21f, 0.0f}, 14.0f, true, 0, false},
    //At a deco stop after switching to EAN50
    {true, 1.60f, 6.1f, 40.3f, 34 * 60 + 47, 0, 4.8f, true, 6, 3, 8, {0.50f, 0.0f}, 17.0f, true, 0, false},
    //Trimix deco stop
    {true, 3.05f, 21.0f, 55.4f, 41 * 60 + 2, 0, 18.6f, true, 21, 2, 47, {0.18f, 0.45f}, 11.0f, true, 0, false},
};
const int EXAMPLE_COUNT = sizeof(EXAMPLES) / sizeof(EXAMPLES[0]); //Example count; valid indices are 0..EXAMPLE_COUNT-1.
}

void DemoPage::on_enter()
{
    index = 0;
    load();
}

//Formats a fixed DiveView through the live screen renderer and adds its numbered demo tag.
void DemoPage::load(void)
{
    char tag[24];
    snprintf(tag, sizeof(tag), "DEMO %d/%d", index + 1, EXAMPLE_COUNT);
    screen.format(EXAMPLES[index]);
    screen.set_tag(tag);
}

void DemoPage::on_button(Ui &ui, ButtonId id)
{
    if (id == ButtonId::Second) {
        index = (index + 1) % EXAMPLE_COUNT;
        load();
    } else {
        ui.go_home();
    }
}

void DemoPage::draw(u8g_t *u8g)
{
    screen.draw(u8g);
}
