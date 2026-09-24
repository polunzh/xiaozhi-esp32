#include "lcd_display.h"

#if CONFIG_USE_PAGED_CHAT_MESSAGE
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "assets/lang_config.h"
#include "companion_painter.h"
#include "lvgl_theme.h"

namespace {
void Show(lv_obj_t* obj, bool visible) {
    if (visible)
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
void Text(lv_obj_t* obj, const char* text) {
    if (strcmp(lv_label_get_text(obj), text) != 0)
        lv_label_set_text(obj, text);
}
void ScaleText(lv_obj_t* obj, const lv_font_t* font, int line_height, bool centered = false) {
    const int scale = std::max(1, line_height * 256 / std::max<int>(1, font->line_height));
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_transform_pivot_x(obj, centered ? LV_PCT(50) : 0, 0);
    lv_obj_set_style_transform_pivot_y(obj, centered ? LV_PCT(50) : 0, 0);
    lv_obj_set_style_transform_scale_x(obj, scale, 0);
    lv_obj_set_style_transform_scale_y(obj, scale, 0);
}
// Close each color command before wrapping; escape user-supplied markup markers.
std::string CaptionMarkup(const std::string& text, std::pair<size_t, size_t> active) {
    std::string output;
    output.reserve(text.size() * 4);
    for (size_t i = 0; i < text.size();) {
        size_t end = i + 1;
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
            ++end;
        if (text[i] == '#')
            output += "##";
        else {
            bool bright = i >= active.first && i < active.second && text[i] != '\n';
            if (bright)
                output += "#DBFAF7 ";
            output.append(text, i, end - i);
            if (bright)
                output += '#';
        }
        i = end;
    }
    return output;
}
}  // namespace

void LcdDisplay::SetupPagedChat() {
    if (width_ < 640 || height_ < 400)
        return;
    paged_chat_enabled_ = true;
    auto screen = lv_screen_active();
    auto x = [this](int value) { return value * width_ / 800; };
    auto y = [this](int value) { return value * height_ / 480; };
    Show(top_bar_, false);
    Show(emoji_box_, false);
    for (auto obj : {container_, status_bar_, bottom_bar_}) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, 0, 0);
    }
    lv_obj_add_flag(container_, LV_OBJ_FLAG_CLICKABLE);
    for (auto obj : {status_bar_, bottom_bar_}) {
        lv_obj_set_parent(obj, container_);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_add_event_cb(
        container_,
        [](lv_event_t* e) {
            static_cast<LcdDisplay*>(lv_event_get_user_data(e))->DrawCompanion(e);
        },
        LV_EVENT_DRAW_MAIN, this);
    companion_name_ = lv_label_create(container_);
    lv_label_set_text(companion_name_, Lang::Strings::CHAT_NAME);
    lv_obj_set_pos(companion_name_, x(116), y(44));

    lv_obj_set_size(status_bar_, x(704), y(52));
    lv_obj_align(status_bar_, LV_ALIGN_TOP_LEFT, x(48), y(125));
    for (auto label : {status_label_, notification_label_}) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, x(60), 0);
    }
    lv_obj_set_size(bottom_bar_, x(704), y(228));
    lv_obj_align(bottom_bar_, LV_ALIGN_TOP_LEFT, x(48), y(180));
    lv_obj_set_style_clip_corner(bottom_bar_, true, 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(chat_message_label_, true);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);

    page_controls_ = lv_obj_create(container_);
    lv_obj_set_size(page_controls_, width_, y(80));
    lv_obj_align(page_controls_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(page_controls_, 0, 0);
    lv_obj_set_style_border_width(page_controls_, 0, 0);
    lv_obj_set_style_radius(page_controls_, 0, 0);
    lv_obj_remove_flag(page_controls_,
                       static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    page_mode_label_ = lv_label_create(container_);
    lv_obj_align(page_mode_label_, LV_ALIGN_TOP_RIGHT, -x(48), y(125));
    page_hint_label_ = lv_label_create(page_controls_);
    lv_obj_center(page_hint_label_);
    auto button = [&](const char* text, int left, int width) {
        auto obj = lv_button_create(page_controls_);
        lv_obj_set_size(obj, x(width), y(52));
        lv_obj_align(obj, LV_ALIGN_LEFT_MID, x(left), 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, y(15), 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        auto label = lv_label_create(obj);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        return obj;
    };
    page_previous_ = button(Lang::Strings::CHAT_PREVIOUS, 48, 136);
    page_next_ = button(Lang::Strings::CHAT_NEXT, 264, 136);
    page_follow_ = button(Lang::Strings::CHAT_RESUME, 596, 156);
    page_follow_label_ = lv_obj_get_child(page_follow_, 0);
    static const lv_point_precise_t left_arrow[] = {{7, 0}, {0, 8}, {7, 16}};
    static const lv_point_precise_t right_arrow[] = {{0, 0}, {7, 8}, {0, 16}};
    for (auto obj : {page_previous_, page_next_}) {
        bool previous = obj == page_previous_;
        auto label = lv_obj_get_child(obj, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, previous ? x(9) : -x(9), 0);
        auto arrow = lv_line_create(obj);
        lv_line_set_points(arrow, previous ? left_arrow : right_arrow, 3);
        lv_obj_set_style_line_width(arrow, 2, 0);
        lv_obj_set_style_line_rounded(arrow, true, 0);
        lv_obj_set_style_line_color(arrow, CompanionPainter::Cyan(), 0);
        lv_obj_align(arrow, previous ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
                     previous ? x(18) : -x(18), 0);
        lv_obj_add_event_cb(
            obj,
            [](lv_event_t* e) {
                auto self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
                if (lv_event_get_current_target(e) == self->page_previous_)
                    self->paged_chat_.Previous();
                else
                    self->paged_chat_.Next();
                self->RenderPagedChat();
            },
            LV_EVENT_CLICKED, this);
    }
    page_count_label_ = lv_label_create(page_controls_);
    lv_obj_set_width(page_count_label_, x(64));
    lv_obj_set_style_text_align(page_count_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(page_count_label_, LV_ALIGN_CENTER, -x(176), 0);
    lv_obj_add_event_cb(
        page_follow_,
        [](lv_event_t* e) {
            auto self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            self->paged_chat_.Resume();
            self->RenderPagedChat();
        },
        LV_EVENT_CLICKED, this);

    idle_clock_ = lv_obj_create(screen);
    lv_obj_set_size(idle_clock_, width_, height_);
    lv_obj_align(idle_clock_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(idle_clock_, 0, 0);
    lv_obj_set_style_border_width(idle_clock_, 0, 0);
    lv_obj_set_style_pad_all(idle_clock_, 0, 0);
    lv_obj_remove_flag(idle_clock_, LV_OBJ_FLAG_SCROLLABLE);
    Show(idle_clock_, false);
    idle_date_ = lv_label_create(idle_clock_);
    lv_label_set_text(idle_date_, "");
    lv_obj_align(idle_date_, LV_ALIGN_TOP_MID, 0, height_ * 10 / 100);
    idle_hint_ = lv_label_create(idle_clock_);
    lv_label_set_text(idle_hint_, Lang::Strings::CLOCK_WAKE_HINT);
    lv_obj_align(idle_hint_, LV_ALIGN_CENTER, width_ * 5 / 100, height_ * 37 / 100);
    lv_obj_add_event_cb(
        idle_clock_,
        [](lv_event_t* e) {
            static_cast<LcdDisplay*>(lv_event_get_user_data(e))->DrawIdleClock(e);
        },
        LV_EVENT_DRAW_MAIN, this);
    idle_animation_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<LcdDisplay*>(lv_timer_get_user_data(timer))->AnimateIdleClock();
        },
        30, this);
    lv_timer_pause(idle_animation_timer_);
    StylePagedChat(current_theme_);
    RenderPagedChat();
    lv_obj_move_foreground(preview_image_);
    lv_obj_move_foreground(low_battery_popup_);
}

void LcdDisplay::StylePagedChat(Theme* value) {
    auto font = static_cast<LvglTheme*>(value)->text_font()->font();
    for (auto obj : {container_, idle_clock_}) {
        lv_obj_set_style_bg_image_src(obj, nullptr, 0);
        lv_obj_set_style_bg_color(obj, CompanionPainter::BackgroundColor(), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(obj, font, 0);
        lv_obj_set_style_text_color(obj, CompanionPainter::Cyan(), 0);
    }
    for (auto obj : {status_bar_, bottom_bar_, page_controls_})
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    for (auto obj : {status_label_, notification_label_, page_mode_label_, page_hint_label_,
                     page_follow_label_, companion_name_})
        lv_obj_set_style_text_color(obj, CompanionPainter::Cyan(), 0);
    for (auto obj : {page_previous_, page_next_, page_follow_}) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x102D34), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0x245761), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x20545D), LV_STATE_PRESSED);
        lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x102D34), LV_STATE_DISABLED);
        lv_obj_set_style_border_color(obj, lv_color_hex(0x245761), LV_STATE_DISABLED);
        lv_obj_set_style_recolor_opa(obj, 0, LV_STATE_DISABLED);
        auto label = lv_obj_get_child(obj, 0);
        lv_obj_set_style_text_color(label, CompanionPainter::Cyan(), 0);
        ScaleText(label, font, 34 * height_ / 480, true);
    }
    ScaleText(companion_name_, font, 36 * height_ / 480);
    ScaleText(page_mode_label_, font, 30 * height_ / 480);
    lv_obj_set_style_transform_pivot_x(page_mode_label_, LV_PCT(100), 0);
    ScaleText(page_hint_label_, font, 30 * height_ / 480, true);
    ScaleText(page_count_label_, font, 30 * height_ / 480, true);
    lv_obj_set_width(page_count_label_, 80 * width_ * font->line_height / (800 * 30));
    lv_label_set_long_mode(page_count_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(page_count_label_, CompanionPainter::Cyan(), 0);
    ScaleText(idle_date_, font, 44 * height_ / 480, true);
    ScaleText(idle_hint_, font, 40 * height_ / 480, true);
    ScaleText(chat_message_label_, font, 48 * height_ / 480);
    const int scale = std::max<int>(1, 48 * height_ * 256 / (480 * font->line_height));
    page_text_width_ = (width_ - 96 * width_ / 800) * 256 / scale;
    page_text_height_ = 224 * height_ * 256 / (480 * scale);
    page_line_space_ = 10 * height_ * 256 / (480 * scale);
    lv_obj_set_size(chat_message_label_, page_text_width_, page_text_height_);
    lv_obj_set_style_text_line_space(chat_message_label_, page_line_space_, 0);
    if (paged_font_ != font) {
        paged_font_ = font;
        paged_chat_.Reflow([this](const std::string& text) { return PageFits(text); });
    }
    RenderPagedChat();
}

bool LcdDisplay::PageFits(const std::string& text) const {
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), paged_font_, 0, page_line_space_, page_text_width_,
                     LV_TEXT_FLAG_NONE);
    return size.y <= page_text_height_;
}

void LcdDisplay::AppendPagedChat(uint32_t id, const char* text) {
    if (text == nullptr || text[0] == '\0')
        return;
    paged_chat_.Append(id, text, [this](const std::string& value) { return PageFits(value); });
}

void LcdDisplay::RenderPagedChat() {
    auto font = paged_font_;
    const bool answer = paged_response_retained_ && paged_chat_.Count() > 0;
    const bool compact = answer || companion_state_ == CompanionState::Message;
    const bool content = !hide_subtitle_ && paged_chat_.Count() > 0;
    Show(bottom_bar_, content);
    Show(page_controls_, !idle_clock_visible_);
    const bool controls = answer && !hide_subtitle_;
    for (auto obj : {page_previous_, page_next_, page_follow_, page_count_label_})
        Show(obj, controls);
    Show(page_mode_label_, answer);
    Show(page_hint_label_, !answer);
    if (paged_chat_.Index() == 0)
        lv_obj_add_state(page_previous_, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(page_previous_, LV_STATE_DISABLED);
    if (paged_chat_.Count() == 0 || paged_chat_.Index() + 1 >= paged_chat_.Count())
        lv_obj_add_state(page_next_, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(page_next_, LV_STATE_DISABLED);
    const bool following = paged_chat_.Following();
    Text(page_follow_label_,
         following ? Lang::Strings::CHAT_FOLLOW_ACTIVE : Lang::Strings::CHAT_RESUME);
    lv_obj_set_style_bg_color(page_follow_,
                              following ? lv_color_hex(0x102D34) : CompanionPainter::Cyan(), 0);
    lv_obj_set_style_bg_color(page_follow_,
                              following ? lv_color_hex(0x20545D) : lv_color_hex(0x7BF3EF),
                              LV_STATE_PRESSED);
    lv_obj_set_style_text_color(
        page_follow_label_,
        following ? CompanionPainter::Cyan() : CompanionPainter::BackgroundColor(), 0);
    lv_obj_set_style_border_color(page_follow_,
                                  following ? lv_color_hex(0x245761) : CompanionPainter::Cyan(), 0);
    char count[32];
    snprintf(count, sizeof(count), "%u / %u", static_cast<unsigned>(paged_chat_.Index() + 1),
             static_cast<unsigned>(paged_chat_.Count()));
    Text(page_count_label_, count);
    for (auto label : {status_label_, notification_label_}) {
        const int logical_width = (answer ? 360 : 704) * font->line_height / (compact ? 36 : 54);
        ScaleText(label, font, (compact ? 36 : 54) * height_ / 480);
        lv_obj_set_width(label, logical_width * width_ / 800);
        lv_obj_set_style_text_align(label, compact ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, compact ? 60 * width_ / 800 : 0, 0);
    }
    lv_obj_align(status_bar_, LV_ALIGN_TOP_LEFT, 48 * width_ / 800,
                 (compact ? 125 : 145) * height_ / 480);
    lv_obj_align(bottom_bar_, LV_ALIGN_TOP_LEFT, 48 * width_ / 800,
                 (compact ? 180 : 276) * height_ / 480);
    lv_obj_set_height(bottom_bar_, (compact ? 228 : 120) * height_ / 480);
    lv_obj_set_style_text_align(chat_message_label_,
                                compact ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    auto active = companion_state_ == CompanionState::Speaking ? paged_chat_.ActiveBytes()
                                                               : std::pair<size_t, size_t>{0, 0};
    const bool highlighting = active.second > active.first;
    lv_obj_set_style_text_color(
        chat_message_label_,
        highlighting ? CompanionPainter::MutedColor() : CompanionPainter::TextColor(), 0);
    Text(chat_message_label_, CaptionMarkup(paged_chat_.Text(), active).c_str());
    if (answer) {
        Text(page_mode_label_,
             paged_chat_.Following() ? Lang::Strings::CHAT_FOLLOW : Lang::Strings::CHAT_REVIEW);

    } else {
        Text(page_mode_label_, "");
        const char* hint =
            companion_state_ == CompanionState::Listening  ? Lang::Strings::CHAT_LISTEN_HINT
            : companion_state_ == CompanionState::Thinking ? Lang::Strings::CHAT_THINK_HINT
                                                           : "";
        Text(page_hint_label_, hint);
    }

    lv_obj_invalidate(container_);
}

void LcdDisplay::SetPagedMessage(const char* role, const char* content) {
    paged_response_retained_ = false;
    paged_chat_.Reset();
    if (role && strcmp(role, "user") == 0 && content && content[0]) {
        companion_state_ = CompanionState::Thinking;
        Text(status_label_, Lang::Strings::CHAT_THINK);
    } else if (content && content[0]) {
        companion_state_ = CompanionState::Message;
    }
    AppendPagedChat(0, content);
    RenderPagedChat();
}

void LcdDisplay::SetStatus(const char* status) {
    if (!paged_chat_enabled_) {
        LvglDisplay::SetStatus(status);
        return;
    }
    DisplayLockGuard lock(this);
    const char* text = status;
    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        companion_state_ = CompanionState::Listening;
        text = Lang::Strings::CHAT_LISTEN;
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        companion_state_ = CompanionState::Speaking;
        text = Lang::Strings::CHAT_SPEAK;
    } else if (strcmp(status, Lang::Strings::CONNECTING) == 0) {
        companion_state_ = CompanionState::Connecting;
        text = Lang::Strings::CHAT_CONNECT;
    } else if (!idle_clock_visible_)
        companion_state_ = CompanionState::Message;
    LvglDisplay::SetStatus(text);
    RenderPagedChat();
}

void LcdDisplay::SetIdleMode(bool idle) {
    if (!paged_chat_enabled_)
        return;
    DisplayLockGuard lock(this);
    if (idle_clock_visible_ == idle)
        return;
    idle_clock_visible_ = idle;
    if (idle) {
        idle_animation_start_ = lv_tick_get();
        idle_signal_phase_ = 0;
        idle_smile_amount_ = 0.0f;
        idle_colon_opacity_ = LV_OPA_COVER;
        idle_eye_height_ = 7;
        lv_timer_reset(idle_animation_timer_);
        lv_timer_resume(idle_animation_timer_);
    } else {
        lv_timer_pause(idle_animation_timer_);
    }
    UpdateIdleClock();
    Show(idle_clock_, idle);
    RenderPagedChat();
}

void LcdDisplay::DrawCompanion(lv_event_t* event) {
    CompanionPainter painter(event, container_, width_, height_);
    painter.Background();
    painter.Robot(44, 27);
    painter.Clock(idle_time_text_, 694, 38, 58);
    const bool answer = paged_response_retained_ && paged_chat_.Count() > 0;
    if (answer)
        painter.Wave(72, 142, false);
    else if (companion_state_ == CompanionState::Listening ||
             companion_state_ == CompanionState::Speaking)
        painter.Wave(400, 233, true);
    else if (companion_state_ == CompanionState::Thinking ||
             companion_state_ == CompanionState::Connecting) {
        for (int i = 0; i < 3; ++i)
            painter.Rect(374 + i * 22, 226, 8, 8, 4, CompanionPainter::Cyan());
    }
}

void LcdDisplay::DrawIdleClock(lv_event_t* event) {
    CompanionPainter painter(event, idle_clock_, width_, height_);
    painter.Background();
    painter.Clock(idle_time_text_, 400, 132, 340, idle_colon_opacity_, &idle_colon_area_);
    auto font = lv_obj_get_style_text_font(idle_hint_, LV_PART_MAIN);
    lv_point_t hint_size;
    lv_text_get_size(&hint_size, lv_label_get_text(idle_hint_), font, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    const int hint_width = static_cast<int>(static_cast<int64_t>(hint_size.x) * 40 * height_ * 800 /
                                            (font->line_height * 480 * width_));
    const int robot_x = 440 - hint_width / 2 - 72;
    painter.Robot(robot_x, 387, idle_eye_height_, idle_smile_amount_);
    painter.AntennaSignal(robot_x, 387, idle_signal_phase_);
    idle_robot_area_ = painter.Area(robot_x + 2, 364, 50, 72);
}

void LcdDisplay::AnimateIdleClock() {
    // LVGL timers run under the display lock; only dirty the animated regions.
    if (!idle_clock_visible_)
        return;
    const uint32_t elapsed = lv_tick_get() - idle_animation_start_;
    const lv_opa_t opacity = elapsed % 1000 < 600 ? LV_OPA_COVER : LV_OPA_TRANSP;
    if (opacity != idle_colon_opacity_) {
        idle_colon_opacity_ = opacity;
        lv_obj_invalidate_area(idle_clock_, &idle_colon_area_);
    }

    // Repeat the approved expression every 2.4 seconds, with a 100 ms smile lead-in.
    const uint32_t blink = (elapsed % 2400 + 1400) % 2400;
    if (blink < 60)
        idle_eye_height_ = 7 - blink * 6 / 60;
    else if (blink < 100)
        idle_eye_height_ = 1;
    else if (blink < 180)
        idle_eye_height_ = 1 + (blink - 100) * 6 / 80;
    else
        idle_eye_height_ = 7;

    const uint32_t smile = (elapsed % 2400 + 1500) % 2400;
    const float strength = smile < 1200 ? std::sin(smile * 3.14159265f / 1200) : 0.0f;
    idle_smile_amount_ = strength * strength;
    idle_signal_phase_ = elapsed % 3000;
    lv_obj_invalidate_area(idle_clock_, &idle_robot_area_);
}

void LcdDisplay::UpdateIdleClock() {
    // Called with the display lock. Compare strings to avoid redrawing every tick.
    const time_t now = time(nullptr);
    struct tm local_time = {};
    char clock[sizeof(idle_time_text_)] = "--:--";
    char date[96] = "";
    if (localtime_r(&now, &local_time) != nullptr && local_time.tm_year >= 2025 - 1900) {
        strftime(clock, sizeof(clock), "%H:%M", &local_time);
        const char* weekdays[] = {
            Lang::Strings::CLOCK_SUNDAY,   Lang::Strings::CLOCK_MONDAY,
            Lang::Strings::CLOCK_TUESDAY,  Lang::Strings::CLOCK_WEDNESDAY,
            Lang::Strings::CLOCK_THURSDAY, Lang::Strings::CLOCK_FRIDAY,
            Lang::Strings::CLOCK_SATURDAY,
        };
        snprintf(date, sizeof(date), Lang::Strings::CLOCK_DATE_FORMAT, local_time.tm_year + 1900,
                 local_time.tm_mon + 1, local_time.tm_mday, weekdays[local_time.tm_wday]);
    }
    if (strcmp(idle_time_text_, clock) != 0) {
        snprintf(idle_time_text_, sizeof(idle_time_text_), "%s", clock);
        lv_obj_invalidate(idle_clock_);
        lv_obj_invalidate(container_);
    }
    if (strcmp(lv_label_get_text(idle_date_), date) != 0)
        lv_label_set_text(idle_date_, date);
}

void LcdDisplay::UpdateStatusBar(bool update_all) {
    LvglDisplay::UpdateStatusBar(update_all);
    if (!paged_chat_enabled_)
        return;
    DisplayLockGuard lock(this);
    UpdateIdleClock();
}

void LcdDisplay::BeginChatResponse() {
    if (!paged_chat_enabled_)
        return;
    DisplayLockGuard lock(this);
    paged_response_retained_ = true;
    paged_chat_.Reset();
    RenderPagedChat();
}

void LcdDisplay::AddChatSentence(uint32_t id, const char* text) {
    if (!paged_chat_enabled_) {
        SetChatMessage("assistant", text);
        return;
    }
    DisplayLockGuard lock(this);
    AppendPagedChat(id, text);
    RenderPagedChat();
}

void LcdDisplay::SetChatSentenceDuration(uint32_t id, uint32_t duration_ms) {
    if (!paged_chat_enabled_ || id == 0)
        return;
    DisplayLockGuard lock(this);
    auto previous = paged_chat_.Index();
    auto active = paged_chat_.ActiveBytes();
    paged_chat_.SetDuration(id, duration_ms);
    if (previous != paged_chat_.Index() || active != paged_chat_.ActiveBytes())
        RenderPagedChat();
}

void LcdDisplay::SetChatPlaybackPosition(uint32_t id, uint32_t position_ms) {
    if (!paged_chat_enabled_)
        return;
    DisplayLockGuard lock(this);
    auto previous = paged_chat_.Index();
    auto active = paged_chat_.ActiveBytes();
    paged_chat_.Advance(id, position_ms);
    if (previous != paged_chat_.Index() || active != paged_chat_.ActiveBytes())
        RenderPagedChat();
}
#endif
