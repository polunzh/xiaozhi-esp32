#ifndef PAGED_CHAT_H
#define PAGED_CHAT_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

// UI-task-owned model. Layout is measured by the caller using the actual display font.
class PagedChat {
public:
    static constexpr size_t kMaxPages = 32;
    static constexpr size_t kMaxSentenceBytes = 8192;

    static size_t Characters(const std::string& text) {
        size_t count = 0;
        for (unsigned char c : text) {
            if ((c & 0xc0) != 0x80)
                ++count;
        }
        return count;
    }

    void Reset() {
        pages_.clear();
        visible_ = playing_ = omitted_ = 0;
        last_id_ = last_ms_ = 0;
        following_ = true;
        truncated_ = false;
    }

    template <class Fits>
    void Append(uint32_t id, const std::string& input, Fits fits) {
        std::string text;
        text.reserve(std::min(input.size(), kMaxSentenceBytes));
        // Normalize malformed UTF-8 and bound a single network-supplied sentence.
        for (size_t i = 0; i < input.size() && text.size() < kMaxSentenceBytes;) {
            unsigned char c = input[i];
            size_t n = c < 0x80                 ? 1
                       : c >= 0xc2 && c <= 0xdf ? 2
                       : c >= 0xe0 && c <= 0xef ? 3
                       : c >= 0xf0 && c <= 0xf4 ? 4
                                                : 0;
            bool valid = n && i + n <= input.size();
            for (size_t j = 1; valid && j < n; ++j) {
                valid = (static_cast<unsigned char>(input[i + j]) & 0xc0) == 0x80;
            }
            if (valid && n >= 3) {
                auto second = static_cast<unsigned char>(input[i + 1]);
                valid = !(c == 0xe0 && second < 0xa0) && !(c == 0xed && second >= 0xa0) &&
                        !(c == 0xf0 && second < 0x90) && !(c == 0xf4 && second >= 0x90);
            }
            if (!valid) {
                text += '?';
                ++i;
                continue;
            }
            if (text.size() + n > kMaxSentenceBytes) {
                truncated_ = true;
                break;
            }
            if (n == 1 && c < 0x20 && c != '\n')
                text += ' ';
            else
                text.append(input, i, n);
            i += n;
            if (i < input.size() && text.size() == kMaxSentenceBytes)
                truncated_ = true;
        }
        if (text.empty())
            return;
        std::vector<size_t> boundaries;
        for (size_t i = 0; i < text.size(); ++i) {
            if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
                boundaries.push_back(i);
        }
        boundaries.push_back(text.size());
        const size_t total = boundaries.size() - 1;
        size_t start = 0;
        while (start < total) {
            if (pages_.empty())
                pages_.push_back({});
            auto& page = pages_.back();
            std::string prefix = page.text.empty() ? "" : page.text + "\n";
            size_t low = start, high = total;
            while (low < high) {
                size_t mid = low + (high - low + 1) / 2;
                if (fits(prefix +
                         text.substr(boundaries[start], boundaries[mid] - boundaries[start])))
                    low = mid;
                else
                    high = mid - 1;
            }
            if (low == start && !page.text.empty()) {
                NewPage();
                continue;
            }
            if (low == start)
                ++low;  // Always consume even if a glyph exceeds the viewport.
            page.text =
                prefix + text.substr(boundaries[start], boundaries[low] - boundaries[start]);
            page.spans.push_back(
                {id, start, total, 0, prefix.size(), boundaries[low] - boundaries[start]});
            start = low;
            if (start < total)
                NewPage();
        }
        if (last_id_)
            Advance(last_id_, last_ms_);
    }

    template <class Fits>
    void Reflow(Fits fits) {
        if (pages_.empty())
            return;
        auto anchor = pages_[visible_].spans.front();
        PagedChat rebuilt;
        std::string text;
        Span source{};
        auto flush = [&]() {
            if (text.empty())
                return;
            rebuilt.Append(source.id, text, fits);
            for (auto& page : rebuilt.pages_)
                for (auto& span : page.spans) {
                    if (span.id == source.id) {
                        span.start += source.start;
                        span.total = source.total;
                        span.duration = source.duration;
                    }
                }
            text.clear();
        };
        for (const auto& page : pages_)
            for (const auto& span : page.spans) {
                if (text.empty())
                    source = span;
                else if (source.id != span.id) {
                    flush();
                    source = span;
                }
                text.append(page.text, span.byte_start, span.byte_length);
            }
        flush();
        rebuilt.Advance(last_id_, last_ms_);
        if (!following_) {
            rebuilt.following_ = false;
            for (size_t i = 0; i < rebuilt.pages_.size(); ++i) {
                for (const auto& span : rebuilt.pages_[i].spans) {
                    if (span.id == anchor.id && span.start <= anchor.start)
                        rebuilt.visible_ = i;
                }
            }
        }
        rebuilt.truncated_ = rebuilt.truncated_ || truncated_;
        *this = std::move(rebuilt);
    }

    void SetDuration(uint32_t id, uint32_t milliseconds) {
        for (auto& page : pages_)
            for (auto& span : page.spans) {
                if (span.id == id)
                    span.duration = milliseconds;
            }
        if (last_id_ == id)
            Advance(last_id_, last_ms_);
    }

    void Advance(uint32_t id, uint32_t milliseconds) {
        last_id_ = id;
        last_ms_ = milliseconds;
        for (size_t i = playing_; i < pages_.size(); ++i) {
            for (const auto& span : pages_[i].spans) {
                if (span.id != id)
                    continue;
                uint64_t threshold = span.duration
                                         ? uint64_t(span.duration) * span.start / span.total
                                         : uint64_t(span.start) * 180;
                if (milliseconds >= threshold)
                    playing_ = i;
            }
        }
        if (following_)
            visible_ = playing_;
    }

    void Previous() {
        following_ = false;
        if (visible_)
            --visible_;
    }
    void Next() {
        following_ = false;
        if (visible_ + 1 < pages_.size())
            ++visible_;
    }
    void Pause() { following_ = false; }
    void Resume() {
        following_ = true;
        visible_ = playing_;
    }
    bool Following() const { return following_; }
    bool Truncated() const { return truncated_; }
    size_t Count() const { return pages_.size(); }
    size_t Index() const { return visible_; }
    size_t Omitted() const { return omitted_; }
    // Byte offsets in Text(), suitable for coloring UTF-8 without changing layout.
    std::pair<size_t, size_t> ActiveBytes() const {
        if (!following_ || pages_.empty() || last_id_ == 0)
            return {0, 0};
        for (const auto& span : pages_[visible_].spans) {
            if (span.id == last_id_)
                return {span.byte_start, span.byte_start + span.byte_length};
        }
        return {0, 0};
    }
    const std::string& Text() const {
        static const std::string empty;
        return pages_.empty() ? empty : pages_[visible_].text;
    }

private:
    struct Span {
        uint32_t id;
        size_t start;
        size_t total;
        uint32_t duration;
        size_t byte_start;
        size_t byte_length;
    };
    struct Page {
        std::string text;
        std::vector<Span> spans;
    };
    std::deque<Page> pages_;
    size_t visible_ = 0, playing_ = 0, omitted_ = 0;
    uint32_t last_id_ = 0, last_ms_ = 0;
    bool following_ = true, truncated_ = false;

    void NewPage() {
        if (pages_.size() == kMaxPages) {
            pages_.pop_front();
            if (visible_)
                --visible_;
            if (playing_)
                --playing_;
            ++omitted_;
            truncated_ = true;
        }
        pages_.push_back({});
    }
};
#endif
