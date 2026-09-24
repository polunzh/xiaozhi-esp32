"""Run the real application wake methods against deterministic audio/network fakes."""
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def method(name):
    source = (ROOT / 'main/application.cc').read_text()
    start = source.index('void Application::' + name + '(')
    end = source.index('\n}\n', start) + 3
    return source[start:end]


class WakeResponseTest(unittest.TestCase):
    def test_local_ack_precedes_network_and_abort_discards_buffer(self):
        harness = r'''
#include <cassert>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <algorithm>
#define ESP_LOGI(...)
#define CONFIG_LOCAL_WAKE_ACK 1
#define CONFIG_SEND_WAKE_WORD_DATA 1
std::vector<std::string> events;
enum { kDeviceStateIdle, kDeviceStateConnecting, kDeviceStateListening, kDeviceStateSpeaking, kDeviceStateNotifying, kDeviceStateActivating };
enum AbortReason { kAbortReasonNone, kAbortReasonWakeWordDetected };
namespace Lang::Sounds { constexpr auto OGG_WAKE_ACK = "ack"; constexpr auto OGG_POPUP = "popup"; }
enum class PowerSaveLevel { PERFORMANCE };
struct Board {
 static Board& GetInstance() { static Board b; return b; }
 void SetPowerSaveLevel(PowerSaveLevel) {}
};
struct Audio {
 std::string GetLastWakeWord() { return "你好小满"; }
 int PopPacketFromSendQueue() { return 0; }
 void EncodeWakeWord() { events.push_back("encode"); }
 void EnableWakeWordDetection(bool) {}
 void EnableVoiceProcessing(bool) {}
 void ResetDecoder() { events.push_back("reset"); }
 void PlaySound(std::string_view s) { events.emplace_back(s); }
 int PopWakeWordPacket() { return 0; }
};
struct Protocol {
 bool opened = false, succeeds = true;
 bool IsAudioChannelOpened() { return opened; }
 bool OpenAudioChannel() { events.push_back("connect"); return succeeds; }
 void SendAudio(int) {}
 void SendStartListening(int) { events.push_back("listen"); }
 void SendWakeWordDetected(const std::string&) { events.push_back("server_greeting"); }
 void SendAbortSpeaking(AbortReason) { events.push_back("abort"); }
};
struct Application {
 Audio audio_service_; Protocol p; Protocol* protocol_ = &p;
 bool aborted_ = false, play_popup_on_listening_ = false, local_wake_ack_pending_ = false;
 bool pending_listening_start_ = false;
 int listening_mode_ = 0;
 int state = kDeviceStateIdle;
 int GetDeviceState() { return state; }
 bool SetDeviceState(int s) { state = s; return true; }
 int GetDefaultListeningMode() { return 0; }
 void SetListeningMode(int) { state = kDeviceStateListening; }
 void Schedule(std::function<void()> f) { f(); }
 void BeginWakeWordInvoke(const std::string&);
 void ContinueWakeWordInvoke(const std::string&);
 void AbortSpeaking(AbortReason);
 void PlayLocalWakeAck();
 void HandleWakeWordDetectedEvent();
 void StartListeningAudio();
 void ConfigureWakeWordForListening() {}
 void StopNotification() { state = kDeviceStateIdle; }
};
'''
        # The helper is introduced with the local acknowledgement implementation.
        source = (ROOT / 'main/application.cc').read_text()
        if 'void Application::PlayLocalWakeAck(' in source:
            harness += method('PlayLocalWakeAck')
        harness += ''.join(method(n) for n in ('BeginWakeWordInvoke', 'ContinueWakeWordInvoke', 'AbortSpeaking', 'HandleWakeWordDetectedEvent', 'StartListeningAudio'))
        harness += r'''
int main() {
 Application app;
 app.BeginWakeWordInvoke("你好小满");
#if CONFIG_LOCAL_WAKE_ACK
 auto ack = std::find(events.begin(), events.end(), "ack");
 auto connect = std::find(events.begin(), events.end(), "connect");
 assert(ack != events.end() && ack < connect);
 assert(std::find(events.begin(), events.end(), "server_greeting") == events.end());
 assert(app.state == kDeviceStateListening);
 assert(app.local_wake_ack_pending_);
 events.clear();
 app.AbortSpeaking(kAbortReasonWakeWordDetected);
 assert(app.aborted_);
 assert((events == std::vector<std::string>{"reset", "reset", "ack", "abort"}));
 events.clear();
 app.state = kDeviceStateListening;
 app.HandleWakeWordDetectedEvent();
 assert(app.pending_listening_start_);
 assert(std::find(events.begin(), events.end(), "listen") == events.end());
 app.StartListeningAudio();
 assert(!app.local_wake_ack_pending_);
 assert(std::find(events.begin(), events.end(), "listen") != events.end());
 assert(std::find(events.begin(), events.end(), "popup") == events.end());
 events.clear();
 Application offline;
 offline.p.succeeds = false;
 offline.BeginWakeWordInvoke("你好小满");
 assert(std::find(events.begin(), events.end(), "ack") != events.end());
 assert(offline.state == kDeviceStateIdle);
#else
 assert(std::find(events.begin(), events.end(), "encode") != events.end());
 assert(std::find(events.begin(), events.end(), "server_greeting") != events.end());
 assert(std::find(events.begin(), events.end(), "ack") == events.end());
 events.clear();
 app.AbortSpeaking(kAbortReasonWakeWordDetected);
 assert((events == std::vector<std::string>{"reset", "abort"}));
#endif
}
'''
        with tempfile.TemporaryDirectory() as d:
            src, exe = Path(d) / 'test.cc', Path(d) / 'test'
            for local_ack in (0, 1):
                with self.subTest(local_ack=local_ack):
                    src.write_text(harness.replace('#define CONFIG_LOCAL_WAKE_ACK 1',
                                                   f'#define CONFIG_LOCAL_WAKE_ACK {local_ack}'))
                    subprocess.run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++17', str(src), '-o', str(exe)], check=True)
                    subprocess.run([str(exe)], check=True)
