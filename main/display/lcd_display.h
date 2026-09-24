#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "gif/lvgl_gif.h"
#include "lvgl_display.h"
#if CONFIG_USE_PAGED_CHAT_MESSAGE
#include "paged_chat.h"
#endif

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <atomic>
#include <memory>

#define PREVIEW_IMAGE_DURATION_MS 5000

class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles

#if CONFIG_USE_PAGED_CHAT_MESSAGE
    bool paged_chat_enabled_ = false;
    bool paged_response_retained_ = false;
    const lv_font_t* paged_font_ = nullptr;
    PagedChat paged_chat_;
    lv_obj_t* page_controls_ = nullptr;
    lv_obj_t* page_follow_ = nullptr;
    lv_obj_t* page_mode_label_ = nullptr;
    lv_obj_t* page_hint_label_ = nullptr;
    lv_obj_t* companion_name_ = nullptr;
    lv_obj_t* page_previous_ = nullptr;
    lv_obj_t* page_next_ = nullptr;
    lv_obj_t* page_count_label_ = nullptr;
    lv_obj_t* page_follow_label_ = nullptr;
    int page_text_height_ = 0;
    int page_text_width_ = 0;
    int page_line_space_ = 0;
    enum class CompanionState { Message, Connecting, Listening, Thinking, Speaking };
    CompanionState companion_state_ = CompanionState::Message;
    void DrawCompanion(lv_event_t* event);
    void SetPagedMessage(const char* role, const char* content);
    bool PageFits(const std::string& text) const;
    bool idle_clock_visible_ = false;
    lv_obj_t* idle_clock_ = nullptr;
    lv_obj_t* idle_date_ = nullptr;
    lv_obj_t* idle_hint_ = nullptr;
    char idle_time_text_[6] = "--:--";
    void UpdateIdleClock();
    void DrawIdleClock(lv_event_t* event);
    void SetupPagedChat();
    void StylePagedChat(Theme* theme);
    void RenderPagedChat();
    void AppendPagedChat(uint32_t id, const char* text);
#endif
    void InitializeLcdThemes();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height);

public:
    ~LcdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
#if CONFIG_USE_PAGED_CHAT_MESSAGE
    void SetIdleMode(bool idle) override;
    void SetStatus(const char* status) override;
    void UpdateStatusBar(bool update_all = false) override;
    void BeginChatResponse() override;
    void AddChatSentence(uint32_t id, const char* text) override;
    void SetChatSentenceDuration(uint32_t id, uint32_t duration_ms) override;
    void SetChatPlaybackPosition(uint32_t id, uint32_t position_ms) override;
#endif
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;

    // Set whether to hide chat messages/subtitles
    virtual void SetHideSubtitle(bool hide) override;
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy);
};

#endif  // LCD_DISPLAY_H
