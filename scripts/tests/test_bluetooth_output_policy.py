import os
import shlex
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "main" / "audio" / "output"


class BluetoothOutputPolicyTest(unittest.TestCase):
    def test_route_generation_buffer_backoff_and_tail_deadline(self):
        driver = r"""
            #include <cassert>
            #include <cstdint>
            #include <vector>
            #include "bluetooth_output_policy.h"

            int main() {
                BluetoothOutputPolicy policy;
                policy.BeginPlayback(false);
                assert(policy.Route() == AudioOutputRoute::kLocal);
                policy.BeginPlayback(true);
                assert(policy.Route() == AudioOutputRoute::kBluetooth);
                auto generation = policy.Generation();
                policy.Disconnected();
                assert(policy.Route() == AudioOutputRoute::kLocal);
                assert(policy.Generation() != generation);
                generation = policy.Generation();
                policy.Cancel();
                assert(policy.Generation() != generation);

                BluetoothOutputPolicy buffer;
                std::vector<int16_t> input(12000, 7);
                assert(buffer.Write(input.data(), input.size(), buffer.Generation()));
                assert(buffer.BufferedSamples() == input.size());
                assert(!buffer.Write(input.data(), 1, buffer.Generation()));
                std::vector<int16_t> first(6000);
                assert(buffer.Read(first.data(), first.size(), buffer.Generation()) == first.size());
                assert(buffer.Write(input.data(), 6000, buffer.Generation()));
                std::vector<int16_t> second(12000);
                assert(buffer.Read(second.data(), second.size(), buffer.Generation()) == second.size());
                assert(buffer.BufferedSamples() == 0);
                assert(!buffer.Read(second.data(), 1, buffer.Generation() - 1));

                BluetoothOutputPolicy retry;
                retry.Disconnected();
                assert(retry.RetryDelayMs() == 2000);
                retry.Disconnected();
                assert(retry.RetryDelayMs() == 4000);
                retry.Disconnected();
                assert(retry.RetryDelayMs() == 8000);
                retry.Disconnected();
                assert(retry.RetryDelayMs() == 16000);
                retry.Disconnected();
                assert(retry.RetryDelayMs() == 30000);
                retry.Disconnected();
                assert(retry.RetryDelayMs() == 30000);
                retry.ConnectionSucceeded();
                assert(retry.RetryDelayMs() == 2000);

                BluetoothOutputPolicy tail;
                tail.SetRemoteDelayTenthsMs(1400, 1000);
                assert(tail.TailDeadlineMs() == 240);
                tail.SetRemoteDelayTenthsMs(0, 1000);
                assert(tail.TailDeadlineMs() == 500);
                tail.SetRemoteDelayTenthsMs(30000, 1000);
                assert(tail.TailDeadlineMs() == 2000);
                tail.SetRemoteDelayTenthsMs(1400, 1000);
                assert(!tail.IsDrained(1239));
                assert(tail.IsDrained(1240));
            }
        """
        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory)
            source = build_dir / "bluetooth_output_policy_test.cc"
            source.write_text(textwrap.dedent(driver), encoding="utf-8")
            executable = build_dir / "bluetooth_output_policy_test"
            command = shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20",
                f"-I{OUTPUT}",
                str(source),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=build_dir)
            subprocess.run([executable], check=True, cwd=build_dir)


if __name__ == "__main__":
    unittest.main()
