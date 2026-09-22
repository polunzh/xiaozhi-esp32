import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PagedChatTest(unittest.TestCase):
    def test_pagination_playback_and_review(self):
        source = r'''
#include "display/paged_chat.h"
#include <cassert>
#include <string>
int main() {
    auto fits = [](const std::string& text) { return PagedChat::Characters(text) <= 5; };
    PagedChat p;
    p.Append(1, "你好世界。再见世界。", fits);
    assert(p.Count() == 2);
    assert(p.Text() == "你好世界。");
    p.SetDuration(1, 1000);
    p.Advance(1, 499);
    assert(p.Index() == 0);
    p.Advance(1, 500);
    assert(p.Index() == 1);
    p.Previous();
    assert(!p.Following() && p.Index() == 0);
    p.Advance(1, 900);
    assert(p.Index() == 0);
    p.Resume();
    assert(p.Index() == 1 && p.Following());
    p.SetDuration(1, 10000);
    p.Advance(1, 950);
    assert(p.Index() == 1); // late duration cannot rewind
    p.Reset();
    p.Advance(1, 100000);
    assert(p.Count() == 0);
    p.Append(2, "ab", fits);
    p.Append(3, "cd", fits);
    assert(p.Count() == 1 && p.Text() == "ab\ncd");
    p.Append(4, "ef", fits);
    assert(p.Count() == 2 && p.Index() == 0);
    p.Advance(4, 0);
    assert(p.Index() == 1);
    p.Previous();
    p.Append(5, "ghijkl", fits);
    assert(p.Index() == 0);
    p.Advance(5, 100000);
    assert(p.Index() == 0);
    p.Resume();
    assert(p.Index() == p.Count()-1);
    p.Reset();
    p.Append(6, "abcdefghij", fits);
    p.Advance(6, 899);
    assert(p.Index() == 0);
    p.Advance(6, 900);
    assert(p.Index() == 1); // provisional timing when duration is not yet known
    auto narrow = [](const std::string& text) { return PagedChat::Characters(text) <= 2; };
    p.Previous();
    p.Reflow(narrow);
    assert(p.Count() == 5 && p.Index() == 0 && !p.Following());
    p.Resume();
    assert(p.Index() == 2);
    p.Reset();
    p.Append(7, "a\nb\nc", fits);
    assert(p.Text() == "a\nb\nc");
    p.Append(8, std::string("bad\xff"), fits);
    for (int i=9; i<200; ++i) p.Append(i, "12345", fits);
    assert(p.Count() <= PagedChat::kMaxPages);
    assert(p.Index() < p.Count());
    p.Reset();
    p.Append(200, "12345123451234512345", fits);
    p.Advance(200, 940);
    assert(p.Index() == 1);
    p.SetDuration(200, 1000);
    assert(p.Index() == 3); // measured duration arriving after final PCM
    p.Previous();
    p.SetDuration(200, 500);
    assert(p.Index() == 2 && !p.Following());
    p.Reset();
    p.Append(201, std::string(100000, 'x'), fits);
    assert(p.Count() <= PagedChat::kMaxPages);
    assert(p.Truncated());
}
'''
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / 'test.cc'
            exe = Path(temp) / 'test'
            src.write_text(source)
            cmd = shlex.split(os.environ.get('CXX', 'c++'))
            subprocess.run(cmd + ['-std=c++17', '-I' + str(ROOT / 'main'), str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
