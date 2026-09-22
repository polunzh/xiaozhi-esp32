# Paged chat implementation plan

Goal: implement the approved 800×480 sketch in docs/ui-preview/voice-pages.png: small emotion/status header, five lines of left-aligned text, whole-page changes tied to playback, manual history and resume-follow control.

Architecture: an opt-in LCD style uses a bounded, UTF-8-safe pagination model measured with LVGL's actual font. Sentence identifiers and elapsed audio time travel with decoded packets to a coalesced main-loop event. Notification playback and other display styles retain their existing paths.

Constraints: ESP-IDF 6.1; preserve existing worktree edits; no server protocol changes; no main/audio-task blocking on UI; no push. Implement in the current workspace to reuse the verified device configuration. The user has approved the design and asked to start implementation.

## Steps
- [x] Test a host-side pagination model: measured UTF-8 splits, sentence accumulation, playback-driven page selection, manual review/resume, bounded history and reset/stale progress.
- [x] Implement the model and wire the optional LCD layout and controls. Keep the last response visible after speech ends, clear for the next response, and preserve errors, notifications, preview images and theme changes.
- [x] Attach local caption metadata to incoming TTS packets in the shared Application path, propagate through decoding, and report playback through an event bit. Finalize known sentence durations at the next sentence/start-stop boundary. For a still-streaming long sentence, estimate at 180 ms/codepoint until its measured duration is known; never move backwards automatically.
- [x] Enable the option only for the current Korvo S31 variant, run host tests and build the affected configuration, inspect and flash the device, then request a real long-answer/manual-flip check.

## Review focus
- Long Chinese text, English words, explicit newlines and invalid UTF-8 must not split bytes or overflow a page.
- Delayed packets and aborted/new replies must not display stale pages.
- Manual review survives new text and resumes at the currently playing page.
- A bounded history handles long answers without unbounded allocation.
- Existing notification audio, small displays and disabled-option builds remain compatible.

## Validation notes
Sentence boundaries can follow actual audio output. Intra-sentence page boundaries are estimated from text proportions because the server does not provide word timestamps. Actual display timing must be checked on the local speaker; Bluetooth speaker latency is outside this variant.

## Verification
- Host tests: 84 passed (macOS 26 SDK selected for the host C++ linker).
- ESP-IDF v6.1, Korvo S31 build passed; app size 0x3088c0, 23% app partition free.
- Disabled-feature syntax compilation passed for Application, AudioService, LcdDisplay and the optional page implementation. Both network transports compiled in the board build; no wire-format changes.
- Independent review: fixed new-theme colors/font reflow, transient notification/error clearing, and late sentence-duration catch-up. Pagination/reflow and late-duration regression tests passed.
- User confirmed real-device layout, automatic paging and navigation buttons operate normally. Theme switching and very long bounded-history behavior were checked in code/tests; dedicated hardware stress testing remains outstanding.

- Device flash hash verified; boot reached idle with LCD and GT1151 touch initialized. Firmware ELF SHA-256 prefix: 556148871. User subsequently confirmed layout, automatic paging and touch controls work.
