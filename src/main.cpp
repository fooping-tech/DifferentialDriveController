/*
 * MIT License
 *
 * Copyright (c) 2024 Kouhei Ito
 * Copyright (c) 2024 M5Stack Technology CO LTD
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "main.h"
#include "buzzer.h"

M5GFX display;

namespace {

enum class ControlMode : uint8_t {
    Calibration,
    Control,
};

enum class CalibrationPhase : uint8_t {
    FullRange,
    ZeroCenter,
};

struct StickCalibration {
    uint16_t min;
    uint16_t max;
    uint16_t center;
    bool ready;
};

struct ZeroCenterState {
    uint16_t min;
    uint16_t max;
    uint32_t sum;
    uint32_t count;
};

struct BarRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

constexpr uint16_t kRawResolution        = 4096;
constexpr int16_t kNormalScale           = 1000;
constexpr int16_t kBoostScale            = 2000;
constexpr int16_t kDeadzone              = 40;
constexpr uint16_t kCalibRangeThreshold  = (kRawResolution * 3) / 4;
constexpr uint16_t kZeroCenterRangeLimit  = 30;
constexpr uint32_t kCalibrationHoldMs    = 500;
constexpr uint32_t kBeepDurationMs       = 100;
constexpr uint32_t kBeepGapMs            = 100;
constexpr uint32_t kUiMinIntervalMs      = 100;
constexpr uint32_t kLoopIntervalUs       = 10000;

volatile uint8_t Loop_flag = 0;

hw_timer_t *timer = nullptr;

ControlMode control_mode            = ControlMode::Calibration;
CalibrationPhase calibration_phase  = CalibrationPhase::FullRange;
StickCalibration left_calibration   = {UINT16_MAX, 0, kRawResolution / 2, false};
StickCalibration right_calibration  = {UINT16_MAX, 0, kRawResolution / 2, false};
ZeroCenterState left_zero_center    = {UINT16_MAX, 0, 0, 0};
ZeroCenterState right_zero_center   = {UINT16_MAX, 0, 0, 0};
uint32_t calibration_complete_at_ms = 0;
uint32_t zero_center_stable_at_ms   = 0;

void IRAM_ATTR onTimer() {
    Loop_flag = 1;
}

int16_t apply_deadzone(int16_t value, int16_t deadzone) {
    if (value > -deadzone && value < deadzone) return 0;
    return value;
}

void update_calibration(StickCalibration &cal, uint16_t raw) {
    if (raw < cal.min) cal.min = raw;
    if (raw > cal.max) cal.max = raw;
}

bool calibration_range_ok(const StickCalibration &cal) {
    return (cal.max > cal.min) && ((cal.max - cal.min) >= kCalibRangeThreshold);
}

void finalize_calibration(StickCalibration &cal, uint16_t center) {
    cal.center = center;
    cal.ready  = true;
}

void reset_calibration(StickCalibration &cal) {
    cal.min    = UINT16_MAX;
    cal.max    = 0;
    cal.center = kRawResolution / 2;
    cal.ready  = false;
}

void reset_zero_center_state(ZeroCenterState &state) {
    state.min   = UINT16_MAX;
    state.max   = 0;
    state.sum   = 0;
    state.count = 0;
}

void seed_zero_center_state(ZeroCenterState &state, uint16_t raw) {
    state.min   = raw;
    state.max   = raw;
    state.sum   = raw;
    state.count = 1;
}

void update_zero_center_state(ZeroCenterState &state, uint16_t raw) {
    if (state.count == 0) {
        seed_zero_center_state(state, raw);
        return;
    }
    if (raw < state.min) state.min = raw;
    if (raw > state.max) state.max = raw;
    state.sum += raw;
    state.count++;
}

bool zero_center_range_ok(const ZeroCenterState &state) {
    if (state.count == 0) return false;
    return (state.max - state.min) < kZeroCenterRangeLimit;
}

void play_full_range_complete_beep() {
    buzzer_sound(600, kBeepDurationMs);
    delay(kBeepGapMs);
    buzzer_sound(600, kBeepDurationMs);
}

uint16_t zero_center_average(const ZeroCenterState &state) {
    if (state.count == 0) return kRawResolution / 2;
    return static_cast<uint16_t>(state.sum / state.count);
}

void enter_calibration_mode() {
    control_mode                = ControlMode::Calibration;
    calibration_phase           = CalibrationPhase::FullRange;
    calibration_complete_at_ms  = 0;
    zero_center_stable_at_ms    = 0;
    reset_calibration(left_calibration);
    reset_calibration(right_calibration);
    reset_zero_center_state(left_zero_center);
    reset_zero_center_state(right_zero_center);
    display.fillScreen(TFT_BLACK);
    buzzer_sound(600, 200);
    buzzer_sound(440, 200);
}

int16_t map_raw_to_scaled(uint16_t raw, const StickCalibration &cal, int16_t scale) {
    if (!cal.ready) return 0;

    int32_t center = cal.center;
    int32_t value  = 0;

    if (raw >= center) {
        int32_t denom = static_cast<int32_t>(cal.max) - center;
        if (denom <= 0) return 0;
        float ratio = static_cast<float>(raw - center) / static_cast<float>(denom);
        value       = static_cast<int32_t>(ratio * static_cast<float>(scale));
    } else {
        int32_t denom = center - static_cast<int32_t>(cal.min);
        if (denom <= 0) return 0;
        float ratio = static_cast<float>(center - raw) / static_cast<float>(denom);
        value       = -static_cast<int32_t>(ratio * static_cast<float>(scale));
    }

    value = -value;

    if (value > scale) value = scale;
    if (value < -scale) value = -scale;

    return apply_deadzone(static_cast<int16_t>(value), kDeadzone);
}

void draw_calibration_range_screen(uint16_t left_raw, uint16_t right_raw, bool ready_to_switch) {
    static uint32_t last_draw_ms = 0;
    uint32_t now_ms              = millis();
    if (now_ms - last_draw_ms < kUiMinIntervalMs) return;
    last_draw_ms = now_ms;

    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("CALIBRATION");
    display.println("Rotate sticks fully");
    display.println("");
    display.printf("L raw: %4u\n", left_raw);
    display.printf("R raw: %4u\n", right_raw);
    display.printf("L min:%4u max:%4u\n", left_calibration.min, left_calibration.max);
    display.printf("R min:%4u max:%4u\n", right_calibration.min, right_calibration.max);
    if (ready_to_switch) {
        display.println("Range ok, release sticks");
    }
    display.endWrite();
}

void draw_zero_center_screen(uint16_t left_raw, uint16_t right_raw, bool ready_to_switch) {
    static uint32_t last_draw_ms = 0;
    uint32_t now_ms              = millis();
    if (now_ms - last_draw_ms < kUiMinIntervalMs) return;
    last_draw_ms = now_ms;

    uint16_t left_span  = (left_zero_center.count == 0) ? 0 : (left_zero_center.max - left_zero_center.min);
    uint16_t right_span = (right_zero_center.count == 0) ? 0 : (right_zero_center.max - right_zero_center.min);

    display.startWrite();
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("CALIBRATION");
    display.println("Release sticks");
    display.println("Hold still 0.5s");
    display.println("");
    display.printf("L raw:  %4u\n", left_raw);
    display.printf("R raw:  %4u\n", right_raw);
    display.printf("L span: %4u\n", left_span);
    display.printf("R span: %4u\n", right_span);
    if (ready_to_switch) {
        display.println("Center ok, switching...");
    }
    display.endWrite();
}

void draw_value_bar_vertical(int16_t value, int16_t scale, int16_t x, int16_t y, int16_t width, int16_t height,
                             uint16_t color) {
    if (scale <= 0 || width <= 2 || height <= 2) return;

    int16_t clamped = value;
    if (clamped > scale) clamped = scale;
    if (clamped < -scale) clamped = -scale;

    int16_t center     = y + height / 2;
    int16_t inner_x    = x + 1;
    int16_t inner_w    = width - 2;
    int16_t inner_h    = height - 2;
    int32_t half_span  = inner_h / 2;
    int32_t magnitude  = (clamped >= 0) ? clamped : -clamped;
    int32_t bar_length = (magnitude * half_span) / scale;

    display.drawRect(x, y, width, height, TFT_WHITE);
    display.drawFastHLine(x, center, width, TFT_DARKGREY);

    if (bar_length <= 0) return;

    if (clamped >= 0) {
        int16_t bar_y = center - static_cast<int16_t>(bar_length);
        display.fillRect(inner_x, bar_y, inner_w, static_cast<int16_t>(bar_length), color);
    } else {
        int16_t bar_y = center + 1;
        display.fillRect(inner_x, bar_y, inner_w, static_cast<int16_t>(bar_length), color);
    }
}

bool compute_value_bar_vertical_rect(int16_t value, int16_t scale, int16_t x, int16_t y, int16_t width,
                                     int16_t height, BarRect &rect) {
    if (scale <= 0 || width <= 2 || height <= 2) return false;

    int16_t clamped = value;
    if (clamped > scale) clamped = scale;
    if (clamped < -scale) clamped = -scale;

    int16_t center     = y + height / 2;
    int16_t inner_x    = x + 1;
    int16_t inner_w    = width - 2;
    int16_t inner_h    = height - 2;
    int32_t half_span  = inner_h / 2;
    int32_t magnitude  = (clamped >= 0) ? clamped : -clamped;
    int32_t bar_length = (magnitude * half_span) / scale;

    if (bar_length <= 0) return false;

    rect.x = inner_x;
    rect.w = inner_w;
    rect.h = static_cast<int16_t>(bar_length);
    rect.y = (clamped >= 0) ? static_cast<int16_t>(center - bar_length) : static_cast<int16_t>(center + 1);
    return true;
}

void draw_control_screen(int16_t left_cmd, int16_t right_cmd, bool left_boost, bool right_boost) {
    static uint32_t last_draw_ms = 0;
    static int16_t last_left_cmd = 0;
    static int16_t last_right_cmd = 0;
    static int16_t last_left_scale = 0;
    static int16_t last_right_scale = 0;
    static int16_t last_bar_width = 0;
    static int16_t last_bar_height = 0;
    static int16_t last_left_bar_x = 0;
    static int16_t last_right_bar_x = 0;
    static int16_t last_bars_top = 0;
    static bool bars_initialized = false;

    uint32_t now_ms              = millis();
    if (now_ms - last_draw_ms < kUiMinIntervalMs) return;
    last_draw_ms = now_ms;

    int16_t screen_w    = display.width();
    int16_t screen_h    = display.height();
    int16_t text_line_h = 12;
    int16_t bars_top    = text_line_h * 3 + 4;
    int16_t bar_height  = screen_h - bars_top - 2;
    int16_t margin_x    = 4;
    int16_t bar_gap     = 6;
    int16_t bar_area_w  = screen_w - (margin_x * 2);
    int16_t bar_width   = (bar_area_w - bar_gap) / 2;
    int16_t left_bar_x  = margin_x;
    int16_t right_bar_x = left_bar_x + bar_width + bar_gap;
    int16_t text_area_h = text_line_h * 3 + 2;
    int16_t left_scale  = left_boost ? kBoostScale : kNormalScale;
    int16_t right_scale = right_boost ? kBoostScale : kNormalScale;

    bool layout_changed = !bars_initialized || last_bar_width != bar_width || last_bar_height != bar_height ||
                          last_left_bar_x != left_bar_x || last_right_bar_x != right_bar_x ||
                          last_bars_top != bars_top;

    display.startWrite();
    display.fillRect(0, 0, screen_w, text_area_h, TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("CONTROL");
    display.setCursor(0, text_line_h);
    display.printf("L:%+05d %s\n", left_cmd, left_boost ? "x2" : "x1");
    display.setCursor(0, text_line_h * 2);
    display.printf("R:%+05d %s\n", right_cmd, right_boost ? "x2" : "x1");
    if (bar_width > 2 && bar_height > 2) {
        if (layout_changed) {
            display.fillRect(margin_x, bars_top, bar_area_w, bar_height, TFT_BLACK);
        } else {
            BarRect prev_rect;
            if (compute_value_bar_vertical_rect(last_left_cmd, last_left_scale, left_bar_x, bars_top, bar_width,
                                                bar_height, prev_rect)) {
                display.fillRect(prev_rect.x, prev_rect.y, prev_rect.w, prev_rect.h, TFT_BLACK);
            }
            if (compute_value_bar_vertical_rect(last_right_cmd, last_right_scale, right_bar_x, bars_top, bar_width,
                                                bar_height, prev_rect)) {
                display.fillRect(prev_rect.x, prev_rect.y, prev_rect.w, prev_rect.h, TFT_BLACK);
            }
        }
        uint16_t left_color  = (left_cmd >= 0) ? TFT_GREEN : TFT_RED;
        uint16_t right_color = (right_cmd >= 0) ? TFT_GREEN : TFT_RED;
        draw_value_bar_vertical(left_cmd, left_scale, left_bar_x, bars_top, bar_width, bar_height, left_color);
        draw_value_bar_vertical(right_cmd, right_scale, right_bar_x, bars_top, bar_width, bar_height, right_color);
    }
    display.endWrite();

    last_left_cmd = left_cmd;
    last_right_cmd = right_cmd;
    last_left_scale = left_scale;
    last_right_scale = right_scale;
    last_bar_width = bar_width;
    last_bar_height = bar_height;
    last_left_bar_x = left_bar_x;
    last_right_bar_x = right_bar_x;
    last_bars_top = bars_top;
    bars_initialized = true;
}

}  // namespace

void setup() {
    M5.begin();
    Wire1.begin(38, 39);
    Wire1.setClock(400 * 1000);
    M5.update();

    setup_pwm_buzzer();

    display.begin();
    display.setTextWrap(false);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.fillScreen(TFT_BLACK);

    enter_calibration_mode();

    timer = timerBegin(1, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    timerAlarmWrite(timer, kLoopIntervalUs, true);
    timerAlarmEnable(timer);
    delay(100);
}

void loop() {
    while (Loop_flag == 0) {
    }
    Loop_flag = 0;

    M5.update();
    joy_update();

    uint16_t left_raw  = getLeftY();
    uint16_t right_raw = getRightY();

    if (M5.Btn.wasPressed()) {
        enter_calibration_mode();
    }

    if (control_mode == ControlMode::Calibration) {
        if (calibration_phase == CalibrationPhase::FullRange) {
            update_calibration(left_calibration, left_raw);
            update_calibration(right_calibration, right_raw);

            bool range_ok = calibration_range_ok(left_calibration) && calibration_range_ok(right_calibration);
            if (range_ok) {
                if (calibration_complete_at_ms == 0) {
                    calibration_complete_at_ms = millis();
                } else if (millis() - calibration_complete_at_ms >= kCalibrationHoldMs) {
                    calibration_phase          = CalibrationPhase::ZeroCenter;
                    calibration_complete_at_ms = 0;
                    zero_center_stable_at_ms   = 0;
                    reset_zero_center_state(left_zero_center);
                    reset_zero_center_state(right_zero_center);
                    display.fillScreen(TFT_BLACK);
                    play_full_range_complete_beep();
                }
            } else {
                calibration_complete_at_ms = 0;
            }

            draw_calibration_range_screen(left_raw, right_raw, range_ok);
            return;
        }

        update_zero_center_state(left_zero_center, left_raw);
        update_zero_center_state(right_zero_center, right_raw);

        bool left_stable  = zero_center_range_ok(left_zero_center);
        bool right_stable = zero_center_range_ok(right_zero_center);
        bool stable       = left_stable && right_stable;

        if (stable) {
            if (zero_center_stable_at_ms == 0) {
                zero_center_stable_at_ms = millis();
            } else if (millis() - zero_center_stable_at_ms >= kCalibrationHoldMs) {
                finalize_calibration(left_calibration, zero_center_average(left_zero_center));
                finalize_calibration(right_calibration, zero_center_average(right_zero_center));
                control_mode             = ControlMode::Control;
                calibration_phase        = CalibrationPhase::FullRange;
                zero_center_stable_at_ms = 0;
                display.fillScreen(TFT_BLACK);
                buzzer_sound(440, 200);
                buzzer_sound(600, 200);
            }
        } else {
            zero_center_stable_at_ms = 0;
            seed_zero_center_state(left_zero_center, left_raw);
            seed_zero_center_state(right_zero_center, right_raw);
        }

        draw_zero_center_screen(left_raw, right_raw, stable);
        return;
    }

    int16_t left_scale  = getOptionButton() ? kBoostScale : kNormalScale;
    int16_t right_scale = getModeButton() ? kBoostScale : kNormalScale;

    int16_t left_cmd  = map_raw_to_scaled(left_raw, left_calibration, left_scale);
    int16_t right_cmd = map_raw_to_scaled(right_raw, right_calibration, right_scale);

    USBSerial.printf("L:%d,R:%d\n", left_cmd, right_cmd);

    draw_control_screen(left_cmd, right_cmd, left_scale == kBoostScale, right_scale == kBoostScale);
}
