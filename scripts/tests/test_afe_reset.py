"""Exercise AFE control methods while a feed call owns the input lock."""
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def method(name):
    source = (ROOT / 'main/audio/engines/afe_audio_engine.cc').read_text()
    start = source.index('void AfeAudioEngine::' + name + '(')
    return source[start:source.index('\n}\n', start) + 3]


class AfeResetTest(unittest.TestCase):
    def test_control_and_reset_do_not_wait_for_blocked_feed(self):
        harness = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
using EventBits_t = unsigned;
using namespace std::chrono_literals;
constexpr unsigned kWakeWordEnabled=1, kVoiceProcessingEnabled=2, kAfeActive=4;
constexpr bool kUseAfeForVoiceProcessing=true;
unsigned xEventGroupGetBits(std::atomic<unsigned>* bits) { return bits->load(); }
void xEventGroupSetBits(std::atomic<unsigned>* bits, unsigned mask) { bits->fetch_or(mask); }
void xEventGroupClearBits(std::atomic<unsigned>* bits, unsigned mask) { bits->fetch_and(~mask); }
struct Afe { int resets=0; void reset_buffer(void*) { ++resets; } };
struct AfeAudioEngine {
 std::atomic<unsigned> bits{0}; std::atomic<unsigned>* event_group_=&bits;
 void* afe_data_=this; Afe afe; Afe* afe_iface_=&afe;
 std::atomic<unsigned> control_generation_{0};
 std::atomic<bool> reset_pending_{false}, output_reset_pending_{false}, afe_control_dirty_{false};
 std::mutex input_buffer_mutex_; std::vector<short> input_buffer_{1,2,3};
 void UpdateActiveState(); void ApplyPendingReset();
};
'''
        harness += method('UpdateActiveState') + method('ApplyPendingReset')
        harness += r'''
int main() {
 AfeAudioEngine engine;
 std::promise<void> entered;
 // A hardware feed can block until the fetch task drains its ring buffer.
 std::thread feeder([&] {
   std::lock_guard<std::mutex> lock(engine.input_buffer_mutex_);
   entered.set_value();
   std::this_thread::sleep_for(250ms);
 });
 entered.get_future().wait();
 auto start=std::chrono::steady_clock::now();
 engine.UpdateActiveState();
 assert(std::chrono::steady_clock::now()-start < 100ms);
 assert(engine.reset_pending_);
 engine.ApplyPendingReset();
 assert(std::chrono::steady_clock::now()-start < 100ms);
 assert(engine.reset_pending_ && engine.afe.resets==0);
 feeder.join();
 engine.ApplyPendingReset();
 assert(!engine.reset_pending_ && engine.afe.resets==1);
 assert(engine.input_buffer_.empty());
 engine.ApplyPendingReset();
 assert(engine.afe.resets==1);
 engine.bits=kWakeWordEnabled;
 engine.UpdateActiveState();
 assert(engine.bits & kAfeActive);
}
'''
        with tempfile.TemporaryDirectory() as d:
            src, exe = Path(d)/'test.cc', Path(d)/'test'
            src.write_text(harness)
            subprocess.run([*shlex.split(os.environ.get('CXX','c++')), '-std=c++17', '-pthread', str(src), '-o', str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=5)
