/*
Three-button menu navigation and staged numeric or choice editors.
Provides gas, decompression, clock and display-setting pages.
*/
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "configComputer.h"
#include "diveLog.h"
#include "menu.h"
#include "rtc.h"
#include "units.h"

namespace {
const int ROW_H = 10; //Row height, pixels.
const int TITLE_H = 11; //Title area height, pixels.
const int MENU_ROWS = 5; //Visible rows below the title on a 128x64 display.

//Draws the title and separator across the 128-pixel-wide display.
void draw_title(u8g_t *u8g, const char *title)
{
    //Uses u8g grey indices 0..3; 3 is bright, 2 dimmed and 0 dark.
    u8g_SetColorIndex(u8g, 3);
    u8g_DrawStr(u8g, 2, 0, title);
    u8g_DrawHLine(u8g, 0, TITLE_H - 2, 128);
}

//One row: label on the left, optional value on the right, inverted when highlighted.
void draw_row(u8g_t *u8g, int y, const char *label, const char *value, bool highlight)
{
    u8g_SetColorIndex(u8g, 3);
    if (highlight) {
        u8g_DrawBox(u8g, 0, y, 128, ROW_H);
        u8g_SetColorIndex(u8g, 0);
    }
    u8g_DrawStr(u8g, 2, y + 1, label);
    if (value != nullptr && value[0] != '\0') {
        u8g_DrawStr(u8g, 126 - u8g_GetStrWidth(u8g, value), y + 1, value);
    }
}

//Rounds a fractional value to the nearest whole percent.
int percent(float fraction) { return (int) (fraction * 100.0f + 0.5f); }

//Applies Gregorian leap-year rules, including century exceptions.
bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

//Returns month length for year and month 1..12, including leap-day adjustment.
int days_in_month(int year, int month)
{
    static const int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(year)) return 29;
    return DAYS[month - 1];
}

const char *const WATER_NAMES[] = {"FRESH", "SALT"};
const char *const UNIT_NAMES[] = {"METRIC", "IMPERIAL"};
const char *const LANG_NAMES[] = {"FRANCAIS", "ENGLISH"}; //Matches enum lang order: French, English.
} //Anonymous namespace.

//Scrollable menu navigation.

//Maps B1 left to next, B2 middle to back and B3 right to select, keeping selection visible.
void MenuPage::on_button(Ui &ui, ButtonId id)
{
    const int n = count();
    if (n <= 0) {
        ui.back();
        return;
    }
    if (selected >= n) selected = n - 1;

    switch (id) {
    case ButtonId::First:
        selected = (selected + 1) % n;
        break;
    case ButtonId::Second:
        ui.back();
        return;
    case ButtonId::Third:
        select(ui, selected);
        return;
    }
    if (selected < top) top = selected;
    if (selected >= top + MENU_ROWS) top = selected - MENU_ROWS + 1;
}

//Draws up to five rows and splits each tab-delimited label into name and right-aligned value.
void MenuPage::draw(u8g_t *u8g)
{
    u8g_SetFont(u8g, u8g_font_helvR08);
    u8g_SetFontPosTop(u8g);
    draw_title(u8g, title);

    const int n = count();
    if (selected >= n) selected = n > 0 ? n - 1 : 0;
    if (top > selected) top = selected;

    char buf[28];
    for (int row = 0; row < MENU_ROWS && top + row < n; ++row) {
        const int i = top + row;
        label(i, buf, sizeof(buf));
        //The tab character '\t' separates the name from its right-aligned value.
        char *value = strchr(buf, '\t');
        if (value != nullptr) *value++ = '\0';
        draw_row(u8g, TITLE_H + row * ROW_H, buf, value, i == selected);
    }
    u8g_SetFontPosBaseline(u8g);
}

//Staged field editing.

//Applies dynamic per-field bounds, including dependent gas fractions and calendar dates.
void EditPage::clamp(void)
{
    for (int i = 0; i < count; ++i) {
        if (fields[i].value < min_for(i)) fields[i].value = min_for(i);
        if (fields[i].value > max_for(i)) fields[i].value = max_for(i);
    }
}

//Wraps B1 increments/B2 decrements; B3 advances fields and applies only after the last.
void EditPage::on_button(Ui &ui, ButtonId id)
{
    EditField &f = fields[field];
    const int lo = min_for(field);
    const int hi = max_for(field);

    switch (id) {
    case ButtonId::First:
        f.value = f.value + f.step > hi ? lo : f.value + f.step;
        break;
    case ButtonId::Second:
        f.value = f.value - f.step < lo ? hi : f.value - f.step;
        break;
    case ButtonId::Third:
        if (field + 1 < count) {
            ++field;
        } else {
            clamp();
            apply(ui);
            return;
        }
        break;
    }
    clamp();
}

//Draws choice names or scaled numbers and optional dimmed context information.
void EditPage::draw(u8g_t *u8g)
{
    u8g_SetFont(u8g, u8g_font_helvR08);
    u8g_SetFontPosTop(u8g);
    draw_title(u8g, title);

    char value[16];
    for (int i = 0; i < count; ++i) {
        const EditField &f = fields[i];
        if (f.choices != nullptr) {
            snprintf(value, sizeof(value), "%s", f.choices[f.value - f.min]);
        } else if (f.decimals == 1) {
            snprintf(value, sizeof(value), "%d.%d%s", f.value / 10, f.value % 10, f.suffix ? f.suffix : "");
        } else {
            snprintf(value, sizeof(value), "%d%s", f.value, f.suffix ? f.suffix : "");
        }
        draw_row(u8g, TITLE_H + i * ROW_H, f.label, value, i == field);
    }

    char text[28];
    info(text, sizeof(text));
    if (text[0] != '\0') {
        u8g_SetColorIndex(u8g, 2);
        u8g_DrawStr(u8g, 2, 64 - ROW_H + 1, text);
    }
    u8g_SetFontPosBaseline(u8g);
}

//Concrete gas, dive and system pages.

namespace {

DiveController *dive = nullptr;

//Builds an integer field with inclusive bounds, positive step and optional unit suffix.
void number(EditField &f, const char *label, int value, int min, int max, int step, const char *suffix = nullptr)
{
    f = EditField{label, value, min, max, step, nullptr, 0, suffix};
}

//Builds a named choice field with zero-based indices 0..n-1.
void choice(EditField &f, const char *label, int value, const char *const *names, int n)
{
    f = EditField{label, value, 0, n - 1, 1, names, 0, nullptr};
}

/*
Stages oxygen/helium percentages for an existing gas or a new gas.
The helium upper bound tracks the selected oxygen fraction.
*/
class MixEditPage final : public EditPage {
public:
    //Copies gas_index into staged percentage fields; -1 starts a new EAN32 mixture.
    void open(Ui &ui, int gas_index)
    {
        index = gas_index;
        const GasMix g = index < 0 ? GasMix{0.32f, 0.0f} : dive->gases().get(index);
        if (index < 0) snprintf(title, sizeof(title), "ADD MIX");
        else snprintf(title, sizeof(title), "EDIT MIX %d", index + 1);
        number(fields[0], "O2", percent(g.o2), 5, 100, 1, "%");
        number(fields[1], "HE", percent(g.he), 0, 95, 1, "%");
        count = 2;
        ui.show(*this);
    }
protected:
    //Limits helium so oxygen plus helium cannot exceed 100 percent.
    int max_for(int i) const override
    {
        return i == 1 ? 100 - fields[0].value : fields[i].max;
    }
    //Displays MOD from ppO2 divided by oxygen fraction, converted from bar to depth.
    void info(char *buf, size_t len) const override
    {
        const DecoSettings &s = dive->settings();
        const float ppo2 = index == 0 ? s.max_ppo2_bottom : s.max_ppo2_deco;
        float mod = (ppo2 / (fields[0].value / 100.0f) - dive->surface_pressure()) / s.bar_per_m;
        if (mod < 0.0f) mod = 0.0f;
        char depth[12];
        units::format_depth(mod, configComputer.get_system_unit(), depth, sizeof(depth));
        snprintf(buf, len, "MOD %s (PPO2 %d.%d)", depth, (int) (ppo2 * 10.0f + 0.5f) / 10,
                 (int) (ppo2 * 10.0f + 0.5f) % 10);
    }
    //Converts confirmed percentages to fractions and adds or updates the selected gas.
    void apply(Ui &ui) override
    {
        const GasMix g = {fields[0].value / 100.0f, fields[1].value / 100.0f};
        if (index < 0) {
            dive->gases().add(g);
            ui.back();
        } else {
            dive->gases().set(index, g);
            ui.back(); //Returns to the action menu.
            ui.back(); //Returns to the mix list.
        }
    }
private:
    int index = -1;
};
MixEditPage mixEdit;

/*
Offers use, edit and optional delete actions for a surface-selected gas.
*/
class MixActionMenu final : public MenuPage {
public:
    //Stores gas_index and pushes its action menu onto the page stack.
    void open(Ui &ui, int gas_index)
    {
        index = gas_index;
        snprintf(title, sizeof(title), "MIX %d", index + 1);
        ui.show(*this);
    }
protected:
    int count() const override { return index > 0 ? 3 : 2; }
    void label(int i, char *buf, size_t len) const override
    {
        static const char *const NAMES[] = {"USE FOR DIVE", "EDIT", "DELETE"};
        snprintf(buf, len, "%s", NAMES[i]);
    }
    //Uses the gas, opens its editor or deletes it; bottom gas has no delete action.
    void select(Ui &ui, int i) override
    {
        if (i == 0) {
            dive->switch_gas(index);
            ui.go_home();
        } else if (i == 1) {
            mixEdit.open(ui, index);
        } else {
            dive->gases().remove(index);
            ui.back();
        }
    }
private:
    int index = 0;
};
MixActionMenu mixAction;

/*
Lists carried gases and switches directly during a dive.
At the surface, opens gas actions or offers adding a gas within capacity.
*/
class MixListMenu final : public MenuPage {
public:
    MixListMenu() { snprintf(title, sizeof(title), "MIX"); }
protected:
    //Offers a new-gas row only at the surface and below the five-gas capacity.
    bool can_add() const
    {
        return !dive->in_dive() && dive->gases().count() < GasList::MAX_GASES;
    }
    int count() const override { return dive->gases().count() + (can_add() ? 1 : 0); }
    //Builds a tab-delimited gas label and marks the active gas with an asterisk.
    void label(int i, char *buf, size_t len) const override
    {
        const GasList &gases = dive->gases();
        if (i >= gases.count()) { //The extra row adds a gas; can_add enforces capacity.
            snprintf(buf, len, "ADD MIX");
            return;
        }
        char name[12];
        units::format_gas(gases.get(i), name, sizeof(name));
        snprintf(buf, len, "MIX %d\t%s%s", i + 1, name, i == gases.active_index() ? " *" : "");
    }
    //Opens add/actions at surface or switches gas and returns home during a dive.
    void select(Ui &ui, int i) override
    {
        if (i >= dive->gases().count()) {
            mixEdit.open(ui, -1);
        } else if (dive->in_dive()) {
            dive->switch_gas(i);
            ui.go_home();
        } else {
            mixAction.open(ui, i);
        }
    }
};
MixListMenu mixList;

/*
Stages gradient factors as percentages and enforces GF high >= GF low.
*/
class GfEditPage final : public EditPage {
public:
    //Copies current fractional gradient factors into staged whole-percent fields.
    void open(Ui &ui)
    {
        snprintf(title, sizeof(title), "GRADIENT FACTORS");
        number(fields[0], "GF LOW", percent(dive->settings().gf_low), 10, 100, 5, "%");
        number(fields[1], "GF HIGH", percent(dive->settings().gf_high), 10, 100, 5, "%");
        count = 2;
        ui.show(*this);
    }
protected:
    int min_for(int i) const override { return i == 1 ? fields[0].value : fields[i].min; }
    //Converts confirmed percentages back to fractional gradient factors.
    void apply(Ui &ui) override
    {
        dive->settings().gf_low = fields[0].value / 100.0f;
        dive->settings().gf_high = fields[1].value / 100.0f;
        ui.back();
    }
};
GfEditPage gfEdit;

/*
Stages water type and updates the metric pressure/depth conversion on confirmation.
*/
class SalinityEditPage final : public EditPage {
public:
    //Stages the configured fresh/salt water choice without changing active settings.
    void open(Ui &ui)
    {
        snprintf(title, sizeof(title), "SALINITY");
        choice(fields[0], "WATER", static_cast<int>(configComputer.get_water_type()), WATER_NAMES, 2);
        count = 1;
        ui.show(*this);
    }
protected:
    //Commits water type and refreshes bar per metre from the configured hydrostatic factor.
    void apply(Ui &ui) override
    {
        configComputer.set_water_type(static_cast<waterType>(fields[0].value));
        dive->settings().bar_per_m = configComputer.get_hydrostatic_pressure() / 10.0f;
        ui.back();
    }
};
SalinityEditPage salinityEdit;

/*
Stages bottom and deco ppO2 limits in tenths of a bar.
*/
class Ppo2EditPage final : public EditPage {
public:
    //Stages ppO2 limits as integers 10..16 displayed as 1.0..1.6 bar.
    void open(Ui &ui)
    {
        snprintf(title, sizeof(title), "PPO2 MAX");
        number(fields[0], "BOTTOM", (int) (dive->settings().max_ppo2_bottom * 10.0f + 0.5f), 10, 16, 1);
        number(fields[1], "DECO", (int) (dive->settings().max_ppo2_deco * 10.0f + 0.5f), 10, 16, 1);
        fields[0].decimals = 1;
        fields[1].decimals = 1;
        count = 2;
        ui.show(*this);
    }
protected:
    //Converts confirmed tenths of a bar back to floating-point ppO2 limits.
    void apply(Ui &ui) override
    {
        dive->settings().max_ppo2_bottom = fields[0].value / 10.0f;
        dive->settings().max_ppo2_deco = fields[1].value / 10.0f;
        ui.back();
    }
};
Ppo2EditPage ppo2Edit;

/*
Routes surface decompression settings to GF, salinity and ppO2 editors.
*/
class DiveMenu final : public MenuPage {
public:
    DiveMenu() { snprintf(title, sizeof(title), "DIVE"); }
protected:
    int count() const override { return 3; }
    //Formats current gradient factors, water type or ppO2 limits beside each menu name.
    void label(int i, char *buf, size_t len) const override
    {
        const DecoSettings &s = dive->settings();
        switch (i) {
        case 0:
            snprintf(buf, len, "GF\t%d/%d", percent(s.gf_low), percent(s.gf_high));
            break;
        case 1:
            snprintf(buf, len, "SALINITY\t%s", WATER_NAMES[static_cast<int>(configComputer.get_water_type())]);
            break;
        default: {
            const int b = (int) (s.max_ppo2_bottom * 10.0f + 0.5f);
            const int d = (int) (s.max_ppo2_deco * 10.0f + 0.5f);
            snprintf(buf, len, "PPO2\t%d.%d/%d.%d", b / 10, b % 10, d / 10, d % 10);
            break;
        }
        }
    }
    //Pushes the requested decompression-setting editor.
    void select(Ui &ui, int i) override
    {
        if (i == 0) gfEdit.open(ui);
        else if (i == 1) salinityEdit.open(ui);
        else ppo2Edit.open(ui);
    }
};
DiveMenu diveMenu;

/*
Stages a UTC calendar with month-aware day bounds; no timezone conversion.
*/
class ClockEditPage final : public EditPage {
public:
    //Copies the UTC RTC calendar into staged year, month, day, hour and minute fields.
    void open(Ui &ui)
    {
        snprintf(title, sizeof(title), "DATE / TIME");
        const time_t now = rtc.now();
        struct tm t;
        gmtime_r(&now, &t);
        int year = t.tm_year + 1900;
        if (year < 2024 || year > 2099) year = 2025;
        number(fields[0], "YEAR", year, 2024, 2099, 1);
        number(fields[1], "MONTH", t.tm_mon + 1, 1, 12, 1);
        number(fields[2], "DAY", t.tm_mday, 1, 31, 1);
        number(fields[3], "HOUR", t.tm_hour, 0, 23, 1);
        number(fields[4], "MINUTE", t.tm_min, 0, 59, 1);
        count = 5;
        clamp();
        ui.show(*this);
    }
protected:
    //Clamps the day to the selected month and year, including leap years.
    int max_for(int i) const override
    {
        return i == 2 ? days_in_month(fields[0].value, fields[1].value) : fields[i].max;
    }
    //Commits the staged UTC calendar with seconds zero and marks time as user-confirmed.
    void apply(Ui &ui) override
    {
        struct tm t = {};
        t.tm_year = fields[0].value - 1900;
        t.tm_mon = fields[1].value - 1;
        t.tm_mday = fields[2].value;
        t.tm_hour = fields[3].value;
        t.tm_min = fields[4].value;
        t.tm_isdst = 0;
        rtc.set(mktime(&t), true);
        ui.back();
    }
};
ClockEditPage clockEdit;

/*
Stages display units or language and applies the selected enum only on confirmation.
*/
class ChoiceEditPage final : public EditPage {
public:
    enum Kind { Units, Language };

    //Stages the current units or language enum as a zero-based named choice.
    void open(Ui &ui, Kind k)
    {
        kind = k;
        if (kind == Units) {
            snprintf(title, sizeof(title), "UNITS");
            choice(fields[0], "UNITS", static_cast<int>(configComputer.get_system_unit()), UNIT_NAMES, 2);
        } else {
            snprintf(title, sizeof(title), "LANGUAGE");
            choice(fields[0], "LANGUAGE", static_cast<int>(configComputer.get_lang()), LANG_NAMES, 2);
        }
        count = 1;
        ui.show(*this);
    }
protected:
    //Commits the confirmed display choice and returns to the system menu.
    void apply(Ui &ui) override
    {
        if (kind == Units) configComputer.set_system_unit(static_cast<unitSystem>(fields[0].value));
        else configComputer.set_lang(static_cast<lang>(fields[0].value));
        ui.back();
    }
private:
    Kind kind = Units;
};
ChoiceEditPage choiceEdit;

/*
Routes clock, display-unit and language settings.
*/
class SystemMenu final : public MenuPage {
public:
    SystemMenu() { snprintf(title, sizeof(title), "SYSTEM"); }
protected:
    int count() const override { return 3; }
    //Shows UTC clock status or the selected units/language as a right-aligned value.
    void label(int i, char *buf, size_t len) const override
    {
        switch (i) {
        case 0:
            if (rtc.needs_setting()) {
                snprintf(buf, len, "CLOCK\tNOT SET");
            } else {
                const time_t now = rtc.now();
                struct tm t;
                gmtime_r(&now, &t);
                snprintf(buf, len, "CLOCK\t%02d/%02d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
            }
            break;
        case 1:
            snprintf(buf, len, "UNITS\t%s", UNIT_NAMES[static_cast<int>(configComputer.get_system_unit())]);
            break;
        default:
            snprintf(buf, len, "LANGUAGE\t%s", LANG_NAMES[static_cast<int>(configComputer.get_lang())]);
            break;
        }
    }
    //Opens the corresponding clock, units or language editor.
    void select(Ui &ui, int i) override
    {
        if (i == 0) clockEdit.open(ui);
        else if (i == 1) choiceEdit.open(ui, ChoiceEditPage::Units);
        else choiceEdit.open(ui, ChoiceEditPage::Language);
    }
};
SystemMenu systemMenu;

/*
Shows the selected completed dive's summary, with RTC-compatible UTC timestamps.
*/
class DiveDetailPage final : public Page {
public:
    void open(Ui &ui, int index)
    {
        has_record_ = diveLog.get(index, record_);
        if (has_record_) snprintf(title_, sizeof(title_), "DIVE %02d", index + 1);
        else snprintf(title_, sizeof(title_), "LOG ERROR");
        ui.show(*this);
    }

    void open_log(Ui &ui, const DiveLogRecord &record)
    {
        record_ = record;
        has_record_ = true;
        snprintf(title_, sizeof(title_), "DIVE");
        ui.show(*this);
    }

    void on_button(Ui &ui, ButtonId id) override
    {
        if (id == ButtonId::Second) ui.back();
    }

    void draw(u8g_t *u8g) override
    {
        u8g_SetFont(u8g, u8g_font_helvR08);
        u8g_SetFontPosTop(u8g);
        draw_title(u8g, title_);
        if (!has_record_) {
            u8g_DrawStr(u8g, 2, TITLE_H + 2, "LOG READ ERROR");
            u8g_SetFontPosBaseline(u8g);
            return;
        }

        char max_depth[12], average_depth[12], value[28];
        units::format_depth(record_.max_depth_m, configComputer.get_system_unit(),
                            max_depth, sizeof(max_depth));
        units::format_depth(record_.average_depth_m, configComputer.get_system_unit(),
                            average_depth, sizeof(average_depth));
        snprintf(value, sizeof(value), "MAX %s", max_depth);
        u8g_DrawStr(u8g, 2, 13, value);
        snprintf(value, sizeof(value), "AVG %s", average_depth);
        u8g_DrawStr(u8g, 2, 23, value);
        snprintf(value, sizeof(value), "TIME %lu:%02lu",
                 (unsigned long) (record_.duration_s / 60),
                 (unsigned long) (record_.duration_s % 60));
        u8g_DrawStr(u8g, 2, 33, value);
        draw_timestamp(u8g, "START", record_.start_time, 43);
        draw_timestamp(u8g, "END", record_.end_time, 53);
        u8g_SetFontPosBaseline(u8g);
    }

private:
    static void draw_timestamp(u8g_t *u8g, const char *label, time_t timestamp, int y)
    {
        struct tm t;
        if (gmtime_r(&timestamp, &t) == nullptr) {
            char value[28];
            snprintf(value, sizeof(value), "%s --/--/-- --:--", label);
            u8g_DrawStr(u8g, 2, y, value);
            return;
        }
        //Sized for GCC's worst-case int widths so -Wformat-truncation stays quiet.
        char value[64];
        snprintf(value, sizeof(value), "%s %02d/%02d/%02d %02d:%02d",
                 label, t.tm_mday, t.tm_mon + 1, (t.tm_year + 1900) % 100,
                 t.tm_hour, t.tm_min);
        u8g_DrawStr(u8g, 2, y, value);
    }

    DiveLogRecord record_;
    char title_[20];
    bool has_record_ = false;
};
DiveDetailPage diveDetail;

/*
Scrollable chronological list of completed dives stored on the SD card.
*/
class DiveLogMenu final : public MenuPage {
public:
    DiveLogMenu() : count_(0), read_error_(false), empty_(false), cache_top_(0)
    {
        snprintf(title, sizeof(title), "LOG");
    }

    void on_enter() override
    {
        MenuPage::on_enter();
        read_error_ = !diveLog.count(count_);
        empty_ = !read_error_ && count_ == 0;
        if (read_error_ || empty_) count_ = 1;
        if (empty_) snprintf(title, sizeof(title), "LOG");
        refresh_visible();
        if (empty_) {
            const time_t end = rtc.now();
            records_[0] = DiveLogRecord{end - 45 * 60, end, 45 * 60, 24.6f, 13.2f};
            cached_[0] = true;
        }
    }

    void on_button(Ui &ui, ButtonId id) override
    {
        const int previous_top = top;
        MenuPage::on_button(ui, id);
        if (top != previous_top) refresh_visible();
    }

protected:
    int count() const override { return count_; }

    //Shows dive number, maximum depth, duration in whole minutes and UTC date.
    void label(int i, char *buf, size_t len) const override
    {
        if (read_error_) {
            snprintf(buf, len, "LOG READ ERROR");
            return;
        }
        const int slot = i - cache_top_;
        if (slot < 0 || slot >= 5 || !cached_[slot]) {
            snprintf(buf, len, "READ ERROR");
            return;
        }
        const DiveLogRecord &record = records_[slot];
        char depth[12];
        units::format_depth(record.max_depth_m, configComputer.get_system_unit(),
                            depth, sizeof(depth));
        struct tm date;
        if (gmtime_r(&record.start_time, &date) == nullptr) {
            snprintf(buf, len, "%02d %s %lu' --/--/--", i + 1, depth,
                     (unsigned long) (record.duration_s / 60));
            return;
        }
        snprintf(buf, len, "%02d %s %lu' %02d/%02d/%02d", i + 1, depth,
                 (unsigned long) (record.duration_s / 60), date.tm_mday,
                 date.tm_mon + 1, (date.tm_year + 1900) % 100);
    }

    //Opens the selected dive detail page; informational rows have no action.
    void select(Ui &ui, int i) override
    {
        if (read_error_) return;
        if (empty_) diveDetail.open_log(ui, records_[0]);
        else diveDetail.open(ui, i);
    }

private:
    void refresh_visible()
    {
        cache_top_ = top;
        for (int slot = 0; slot < 5; ++slot) cached_[slot] = false;
        if (read_error_ || empty_) return;

        int loaded = 0;
        if (!diveLog.get_range(cache_top_, records_, 5, loaded)) return;
        for (int slot = 0; slot < 5; ++slot) {
            cached_[slot] = slot < loaded;
        }
    }

    int count_;
    bool read_error_;
    bool empty_;
    int cache_top_;
    DiveLogRecord records_[5];
    bool cached_[5];
};
DiveLogMenu diveLogMenu;

/*
Root menu restricting settings and log access to the surface.
*/
class RootMenu final : public MenuPage {
public:
    RootMenu() { snprintf(title, sizeof(title), "MENU"); }
protected:
    int count() const override { return dive->in_dive() ? 1 : 4; }
    //Writes a root item name; live dives expose only MIX.
    void label(int i, char *buf, size_t len) const override
    {
        static const char *const NAMES[] = {"MIX", "DIVE", "SYSTEM", "LOG"};
        snprintf(buf, len, "%s", NAMES[i]);
    }
    //Pushes the selected gas, dive-setting, system or log page.
    void select(Ui &ui, int i) override
    {
        if (i == 0) ui.show(mixList);
        else if (i == 1) ui.show(diveMenu);
        else if (i == 2) ui.show(systemMenu);
        else ui.show(diveLogMenu);
    }
};
RootMenu rootMenu;

} //Anonymous namespace.

//Binds the shared static page collection to the supplied live controller.
MenuSystem::MenuSystem(DiveController &dive_controller)
{
    dive = &dive_controller;
}

Page &MenuSystem::root(void)
{
    return rootMenu;
}
