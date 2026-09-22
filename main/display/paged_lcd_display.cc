#include "lcd_display.h"

#if CONFIG_USE_PAGED_CHAT_MESSAGE
#include <algorithm>
#include <cstring>

#include "assets/lang_config.h"
#include "lvgl_theme.h"

void LcdDisplay::SetupPagedChat() {
    if (width_ < 640 || height_ < 400)
        return;
    paged_chat_enabled_ = true;
    auto screen = lv_screen_active();
    auto theme = static_cast<LvglTheme*>(current_theme_);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_layout(top_bar_, LV_LAYOUT_NONE, 0);
    lv_obj_set_height(top_bar_, 84);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(network_label_, LV_ALIGN_TOP_RIGHT, -24, 8);
    auto right_icons = lv_obj_get_child(top_bar_, 1);
    lv_obj_align(right_icons, LV_ALIGN_BOTTOM_RIGHT, -24, -8);

    lv_obj_set_parent(emoji_box_, top_bar_);
    lv_obj_set_size(emoji_box_, 48, 48);
    lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(emoji_box_, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_center(emoji_label_);
    lv_obj_center(emoji_image_);
    auto name = lv_label_create(top_bar_);
    lv_label_set_text(name, Lang::Strings::CHAT_NAME);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 88, 0);

    lv_obj_set_size(status_bar_, width_ - 300, 60);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_RIGHT, -88, 12);
    lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_SCROLLABLE);
    for (auto label : {status_label_, notification_label_}) {
        lv_obj_set_width(label, width_ - 300);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_set_size(bottom_bar_, width_, height_ - 84 - 70);
    lv_obj_align(bottom_bar_, LV_ALIGN_TOP_LEFT, 0, 84);
    lv_obj_set_style_pad_hor(bottom_bar_, 32, 0);
    lv_obj_set_style_pad_ver(bottom_bar_, 18, 0);
    lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(chat_message_label_, width_ - 64);
    lv_obj_align(chat_message_label_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(chat_message_label_, 18, 0);
    int line_height = theme->text_font()->font()->line_height;
    page_text_height_ = std::min(height_ - 84 - 70 - 36, 5 * line_height + 4 * 18);
    lv_obj_set_height(chat_message_label_, page_text_height_);

    page_controls_ = lv_obj_create(screen);
    lv_obj_set_size(page_controls_, width_, 70);
    lv_obj_align(page_controls_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(page_controls_, 0, 0);
    lv_obj_set_style_border_width(page_controls_, 0, 0);
    lv_obj_set_style_pad_all(page_controls_, 0, 0);
    lv_obj_remove_flag(page_controls_, LV_OBJ_FLAG_SCROLLABLE);

    auto button = [this](const char* text, int width, int x, lv_align_t align) {
        auto obj = lv_button_create(page_controls_);
        lv_obj_set_size(obj, width, 48);
        lv_obj_align(obj, align, x, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, 6, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        auto label = lv_label_create(obj);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        return obj;
    };
    page_follow_ = button(Lang::Strings::CHAT_FOLLOW, 260, 24, LV_ALIGN_LEFT_MID);
    page_follow_label_ = lv_obj_get_child(page_follow_, 0);
    page_previous_ = button(Lang::Strings::CHAT_PREVIOUS, 112, -244, LV_ALIGN_RIGHT_MID);
    page_next_ = button(Lang::Strings::CHAT_NEXT, 112, -24, LV_ALIGN_RIGHT_MID);
    page_count_label_ = lv_label_create(page_controls_);
    lv_obj_set_width(page_count_label_, 108);
    lv_obj_set_style_text_align(page_count_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(page_count_label_, LV_ALIGN_RIGHT_MID, -136, 0);
    lv_label_set_text(page_count_label_, "");

    lv_obj_add_event_cb(
        page_previous_,
        [](lv_event_t* e) {
            auto self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            self->paged_chat_.Previous();
            self->RenderPagedChat();
        },
        LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(
        page_next_,
        [](lv_event_t* e) {
            auto self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            self->paged_chat_.Next();
            self->RenderPagedChat();
        },
        LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(
        page_follow_,
        [](lv_event_t* e) {
            auto self = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (self->paged_chat_.Following())
                self->paged_chat_.Pause();
            else
                self->paged_chat_.Resume();
            self->RenderPagedChat();
        },
        LV_EVENT_CLICKED, this);
    StylePagedChat(current_theme_);
    RenderPagedChat();
    // Previews and battery alerts must remain above the reading surface.
    lv_obj_move_foreground(preview_image_);
    lv_obj_move_foreground(low_battery_popup_);
}

void LcdDisplay::StylePagedChat(Theme* value) {
    auto theme = static_cast<LvglTheme*>(value);
    for (auto obj : {top_bar_, bottom_bar_, page_controls_}) {
        lv_obj_set_style_bg_color(obj, theme->background_color(), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    }
    for (auto obj : {page_previous_, page_next_, page_follow_}) {
        lv_obj_set_style_text_color(obj, theme->text_color(), 0);
        lv_obj_set_style_bg_color(obj, theme->background_color(), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(obj, theme->text_color(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
    }
    lv_obj_set_style_bg_color(page_follow_, theme->text_color(), 0);
    lv_obj_set_style_bg_opa(page_follow_, LV_OPA_10, 0);
    lv_obj_set_style_text_color(page_count_label_, theme->text_color(), 0);
    lv_obj_set_style_border_side(top_bar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar_, 1, 0);
    lv_obj_set_style_border_color(top_bar_, theme->text_color(), 0);
    lv_obj_set_style_border_opa(top_bar_, LV_OPA_20, 0);
    lv_obj_set_style_border_side(page_controls_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(page_controls_, 1, 0);
    lv_obj_set_style_border_color(page_controls_, theme->text_color(), 0);
    lv_obj_set_style_border_opa(page_controls_, LV_OPA_20, 0);
    auto font = theme->text_font()->font();
    if (font != paged_font_) {
        paged_font_ = font;
        page_text_height_ = std::min<int>(height_ - 84 - 70 - 36, 5 * font->line_height + 4 * 18);
        lv_obj_set_height(chat_message_label_, page_text_height_);
        lv_obj_set_style_text_font(chat_message_label_, font, 0);
        paged_chat_.Reflow([this, font](const std::string& text) {
            lv_point_t size;
            lv_text_get_size(&size, text.c_str(), font, 0, 18, width_ - 64, LV_TEXT_FLAG_NONE);
            return size.y <= page_text_height_;
        });
        RenderPagedChat();
    }
}

void LcdDisplay::AppendPagedChat(uint32_t id, const char* text) {
    if (text == nullptr || text[0] == '\0')
        return;
    auto font = lv_obj_get_style_text_font(chat_message_label_, LV_PART_MAIN);
    const int width = width_ - 64;
    paged_chat_.Append(id, text, [this, font, width](const std::string& value) {
        lv_point_t size;
        lv_text_get_size(&size, value.c_str(), font, 0, 18, width, LV_TEXT_FLAG_NONE);
        return size.y <= page_text_height_;
    });
}

void LcdDisplay::RenderPagedChat() {
    const bool hidden = hide_subtitle_ || paged_chat_.Count() == 0;
    for (auto obj : {bottom_bar_, page_controls_}) {
        if (hidden)
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    auto set_text = [](lv_obj_t* label, const char* text) {
        if (strcmp(lv_label_get_text(label), text) != 0)
            lv_label_set_text(label, text);
    };
    set_text(chat_message_label_, paged_chat_.Text().c_str());
    std::string count = std::to_string(paged_chat_.Omitted() + paged_chat_.Index() + 1) + " / " +
                        std::to_string(paged_chat_.Omitted() + paged_chat_.Count());
    set_text(page_count_label_, count.c_str());
    set_text(page_follow_label_,
             paged_chat_.Following() ? Lang::Strings::CHAT_FOLLOW : Lang::Strings::CHAT_RESUME);
    if (paged_chat_.Index() == 0)
        lv_obj_add_state(page_previous_, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(page_previous_, LV_STATE_DISABLED);
    if (paged_chat_.Index() + 1 >= paged_chat_.Count())
        lv_obj_add_state(page_next_, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(page_next_, LV_STATE_DISABLED);
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
    paged_chat_.SetDuration(id, duration_ms);
    if (previous != paged_chat_.Index())
        RenderPagedChat();
}

void LcdDisplay::SetChatPlaybackPosition(uint32_t id, uint32_t position_ms) {
    if (!paged_chat_enabled_)
        return;
    DisplayLockGuard lock(this);
    auto previous = paged_chat_.Index();
    paged_chat_.Advance(id, position_ms);
    if (previous != paged_chat_.Index())
        RenderPagedChat();
}
#endif
