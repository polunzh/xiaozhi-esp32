#ifndef COMPANION_PAINTER_H
#define COMPANION_PAINTER_H

#include <lvgl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

// Shared 800x480 design coordinates keep clock and conversation visually continuous.
class CompanionPainter {
public:
    CompanionPainter(lv_event_t* event, lv_obj_t* obj, int width, int height)
        : layer_(lv_event_get_layer(event)), width_(width), height_(height) {
        lv_obj_get_coords(obj, &bounds_);
    }
    static lv_color_t Cyan() { return lv_color_hex(0x27E5DF); }
    static lv_color_t BackgroundColor() { return lv_color_hex(0x06151A); }
    static lv_color_t TextColor() { return lv_color_hex(0xDBFAF7); }
    static lv_color_t MutedColor() { return lv_color_hex(0x7DA5AB); }

    void Background() {
        for (int i = 0; i < 24; ++i) {
            const int x = -320 + i * 20;
            const lv_point_t strip[] = {{x, 0}, {x + 20, 0}, {x + 620, 480}, {x + 600, 480}};
            const int shade = 12 - std::abs(12 - i);
            Polygon(strip, 4, lv_color_make(6 + shade, 21 + shade, 26 + shade));
        }
    }
    void Rect(int x, int y, int w, int h, int radius, lv_color_t color, bool outline = false) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.radius = radius * height_ / 480;
        dsc.bg_color = color;
        dsc.bg_opa = outline ? LV_OPA_TRANSP : LV_OPA_COVER;
        dsc.border_color = color;
        dsc.border_width = outline ? std::max(1, 2 * height_ / 480) : 0;
        dsc.border_opa = LV_OPA_COVER;
        const lv_area_t area = {bounds_.x1 + x * width_ / 800, bounds_.y1 + y * height_ / 480,
                                bounds_.x1 + (x + w) * width_ / 800 - 1,
                                bounds_.y1 + (y + h) * height_ / 480 - 1};
        lv_draw_rect(layer_, &dsc, &area);
    }
    lv_area_t Area(int x, int y, int w, int h) const {
        return {bounds_.x1 + x * width_ / 800 - 1, bounds_.y1 + y * height_ / 480 - 1,
                bounds_.x1 + (x + w) * width_ / 800 + 1, bounds_.y1 + (y + h) * height_ / 480 + 1};
    }
    void Robot(int x, int y, int eye_height = 7, float smile_amount = -1.0f) {
        Rect(x, y + 12, 52, 39, 12, Cyan(), true);
        Rect(x + 13, y + 27 + (7 - eye_height) / 2, 7, eye_height, 4, Cyan());
        Rect(x + 33, y + 27 + (7 - eye_height) / 2, 7, eye_height, 4, Cyan());
        Rect(x + 25, y + 5, 2, 8, 1, Cyan());
        Rect(x + 23, y, 6, 6, 3, Cyan());
        Rect(x - 4, y + 24, 2, 14, 1, Cyan());
        Rect(x + 54, y + 24, 2, 14, 1, Cyan());
        if (smile_amount >= 0.0f) {
            // A shallow parabola matches the approved idle smile keyframes.
            const float half_width = 7 + 2 * smile_amount;
            const float corner_y = y + 37 - 1.5f * smile_amount;
            const float depth = 5 + 2 * smile_amount;
            auto point = [&](int step) -> lv_point_precise_t {
                const float u = step / 12.0f;
                return {static_cast<lv_value_precise_t>(
                            bounds_.x1 + (x + 27 - half_width + 2 * half_width * u) * width_ / 800),
                        static_cast<lv_value_precise_t>(
                            bounds_.y1 + (corner_y + 4 * depth * u * (1 - u)) * height_ / 480)};
            };
            lv_draw_line_dsc_t line;
            lv_draw_line_dsc_init(&line);
            line.color = Cyan();
            line.width = std::max(1, 2 * height_ / 480);
            line.round_start = line.round_end = 1;
            for (int i = 0; i < 12; ++i) {
                line.p1 = point(i);
                line.p2 = point(i + 1);
                lv_draw_line(layer_, &line);
            }
            return;
        }
        lv_draw_arc_dsc_t smile;
        lv_draw_arc_dsc_init(&smile);
        smile.color = Cyan();
        smile.center = {bounds_.x1 + (x + 27) * width_ / 800,
                        bounds_.y1 + (y + 35) * height_ / 480};
        smile.radius = 7 * height_ / 480;
        smile.width = std::max(1, 2 * height_ / 480);
        smile.start_angle = 35;
        smile.end_angle = 145;
        smile.rounded = 1;
        lv_draw_arc(layer_, &smile);
    }
    void AntennaSignal(int x, int y, uint32_t phase_ms) {
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.color = Cyan();
        arc.center = {bounds_.x1 + (x + 26) * width_ / 800, bounds_.y1 + (y + 3) * height_ / 480};
        arc.width = std::max(1, 2 * height_ / 480);
        arc.start_angle = 225;
        arc.end_angle = 315;
        arc.rounded = 1;
        // Keep the inner signal visible throughout the entire cycle.
        arc.radius = std::max(1, 9 * height_ / 480);
        arc.opa = 217;
        lv_draw_arc(layer_, &arc);
        for (uint32_t offset : {0U, 1500U}) {
            const float phase = ((phase_ms + offset) % 3000) / 3000.0f;
            const float strength = std::sin(phase * 3.14159265f);
            arc.radius = std::max(1, static_cast<int>((12 + 10 * phase) * height_ / 480));
            arc.opa = static_cast<lv_opa_t>(191 * strength * strength);
            lv_draw_arc(layer_, &arc);
        }
    }
    void Wave(int center_x, int center_y, bool large) {
        static constexpr int heights[] = {10, 20, 32, 44, 28, 18, 10};
        const int step = large ? 16 : 7;
        const int w = large ? 5 : 3;
        for (int i = 0; i < 7; ++i) {
            int h = large ? heights[i] : heights[i] / 2;
            Rect(center_x + (i - 3) * step - w / 2, center_y - h / 2, w, h, w, Cyan());
        }
    }
    void Progress(size_t index, size_t count) {
        if (count < 2)
            return;
        if (count <= 7) {
            int x = 400 - (int(count) * 19 - 1) / 2;
            for (size_t i = 0; i < count; ++i) {
                int w = i == index ? 18 : 6;
                Rect(x, 439, w, 6, 3, i == index ? Cyan() : lv_color_hex(0x28535C));
                x += w + 13;
            }
        } else {
            Rect(350, 441, 100, 3, 2, lv_color_hex(0x28535C));
            Rect(350, 441, std::max(3, int((index + 1) * 100 / count)), 3, 2, Cyan());
        }
    }
    void Clock(const char* time, int center_x, int top, int scale = 256,
               lv_opa_t colon_opacity = LV_OPA_COVER, lv_area_t* colon_area = nullptr) {
        // Tiny polygons lose their solid centers to antialiasing at header size.
        // Use pixel-aligned segments for the small clock.
        if (scale < 100) {
            static constexpr uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                                0x6d, 0x7d, 0x07, 0x7f, 0x6f};
            int x = center_x - 58;
            for (int i = 0; i < 5; ++i) {
                char c = time[i];
                if (c == ':') {
                    Rect(x + 3, top + 10, 4, 4, 1, Cyan());
                    Rect(x + 3, top + 25, 4, 4, 1, Cyan());
                    x += 12;
                    continue;
                }
                uint8_t mask = c >= '0' && c <= '9' ? masks[c - '0'] : 0x40;
                const int segments[7][4] = {{4, 0, 14, 4},  {18, 4, 4, 14}, {18, 22, 4, 14},
                                            {4, 36, 14, 4}, {0, 22, 4, 14}, {0, 4, 4, 14},
                                            {4, 18, 14, 4}};
                for (int j = 0; j < 7; ++j)
                    if (mask & (1 << j)) {
                        const auto& r = segments[j];
                        Rect(x + r[0], top + r[1], r[2], r[3], 1, Cyan());
                    }
                x += 27;
            }
            return;
        }

        static constexpr lv_point_t segments[7][6] = {
            {{17, 0}, {94, 0}, {102, 7}, {85, 22}, {23, 22}, {7, 6}},
            {{104, 12}, {104, 73}, {94, 83}, {82, 72}, {82, 31}, {99, 14}},
            {{104, 94}, {104, 153}, {98, 164}, {82, 148}, {82, 103}, {93, 91}},
            {{16, 168}, {93, 168}, {99, 162}, {83, 146}, {24, 146}, {7, 162}},
            {{0, 95}, {10, 85}, {22, 97}, {22, 144}, {5, 160}, {0, 152}},
            {{0, 16}, {6, 9}, {22, 26}, {22, 70}, {10, 82}, {0, 73}},
            {{17, 73}, {85, 73}, {96, 84}, {85, 95}, {17, 95}, {6, 84}},
        };
        static constexpr uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                            0x6d, 0x7d, 0x07, 0x7f, 0x6f};
        auto advance = [](char c) { return c == '1' ? 44 : (c == ':' ? 62 : 126); };
        int total_width = -22;
        for (int i = 0; i < 5; ++i)
            total_width += advance(time[i]);
        int x = 0;
        const int left = center_x - total_width * scale / 512;
        auto polygon = [&](const lv_point_t* points, int count, int x, int y, lv_color_t color,
                           lv_opa_t opacity = LV_OPA_COVER) {
            lv_point_t scaled[6];
            for (int i = 0; i < count; ++i)
                scaled[i] = {left + (x + points[i].x) * scale / 256,
                             top + (y + points[i].y) * scale / 256};
            Polygon(scaled, count, color, opacity);
        };
        for (int i = 0; i < 5; ++i) {
            const char c = time[i];
            if (c == ':') {
                const lv_point_t dot[] = {{9, 0}, {27, 0}, {27, 18}, {9, 18}};
                polygon(dot, 4, x, 38, Cyan(), colon_opacity);
                polygon(dot, 4, x, 110, Cyan(), colon_opacity);
                if (colon_area != nullptr)
                    *colon_area = Area(left + (x + 9) * scale / 256, top + 38 * scale / 256,
                                       18 * scale / 256 + 1, 90 * scale / 256 + 1);
            } else {
                const uint8_t mask = c >= '0' && c <= '9' ? masks[c - '0'] : 0x40;
                for (int segment = 0; segment < 7; ++segment) {
                    if (mask & (1 << segment))
                        polygon(segments[segment], 6, x - (c == '1' ? 82 : 0), 0, Cyan());
                }
            }
            x += advance(c);
        }
    }

private:
    lv_layer_t* layer_;
    lv_area_t bounds_;
    int width_, height_;
    void Polygon(const lv_point_t* points, int count, lv_color_t color,
                 lv_opa_t opacity = LV_OPA_COVER) {
        lv_draw_triangle_dsc_t dsc;
        lv_draw_triangle_dsc_init(&dsc);
        dsc.color = color;
        dsc.opa = opacity;
        auto point = [&](int i) -> lv_point_precise_t {
            return {static_cast<lv_value_precise_t>(bounds_.x1 + points[i].x * width_ / 800),
                    static_cast<lv_value_precise_t>(bounds_.y1 + points[i].y * height_ / 480)};
        };
        for (int i = 1; i + 1 < count; ++i) {
            dsc.p[0] = point(0);
            dsc.p[1] = point(i);
            dsc.p[2] = point(i + 1);
            lv_draw_triangle(layer_, &dsc);
        }
    }
};
#endif
