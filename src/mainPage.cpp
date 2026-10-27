/*
Live dive and surface page backed by the shared DiveView/DiveScreen renderer.
*/
#include "mainPage.h"
#include "rtc.h"

MainPage::MainPage(DiveController &dive_controller)
    : dive(dive_controller), menu(nullptr), demo(nullptr), screen()
{
}

//Builds the shared DiveView from live metrics and the first required stop, then formats it.
void MainPage::update(float pressure_bar, float temperature_c, time_t now)
{
    const DecoPlan &plan = dive.plan();
    DiveView v = {};
    v.in_dive = dive.in_dive();
    v.pressure_bar = pressure_bar;
    v.depth_m = dive.depth_m();
    v.max_depth_m = dive.max_depth_m();
    v.dive_time_s = dive.dive_time_s();
    v.ndl_min = dive.ndl_min();
    v.ceiling_m = dive.ceiling_m();
    v.missed_deco = dive.missed_deco();
    v.has_stop = plan.count > 0;
    if (v.has_stop) {
        v.stop_depth_m = plan.stops[0].depth_m;
        v.stop_min = plan.stops[0].minutes;
        v.tts_min = plan.tts_min;
    }
    v.gas = dive.gases().active();
    v.temperature_c = temperature_c;
    v.clock_set = !rtc.needs_setting();
    v.now = now;
    screen.format(v);
}

//Allows B2 demos only at surface; other available paths open the dive-aware menu.
void MainPage::on_button(Ui &ui, ButtonId id)
{
    if (id == ButtonId::Second && demo != nullptr && !dive.in_dive()) {
        ui.show(*demo);
    } else if (menu != nullptr) {
        ui.show(*menu);
    }
}

void MainPage::draw(u8g_t *u8g)
{
    screen.draw(u8g);
}
