// license:BSD-3-Clause
//
// Windows の MIDI の口を、時間を区切って開く。
//
// midiInOpen / midiOutOpen は、口の持ち主が固まっていると**返ってこない**。
// 実際、Reason が loopMIDI の口を掴んだまま落ちたあと、loopMIDI の口を開くと
// 何分待っても戻らず、gui.exe が起動の途中で止まったように見えた。
// 呼び出しは取り消せないので、別の糸で開かせて、決めた時間だけ待つ。
// 間に合わなかったら諦める。**遅れて開けてしまったら、その糸が自分で閉じる**
// （待っていた側はもう居ないので、開いたままにすると口を掴みっぱなしになる）。

#ifndef S_MU2000_UI_MM_OPEN_H
#define S_MU2000_UI_MM_OPEN_H

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace ui {

// 開くのを待つ長さ。ふつうの口は数ミリ秒で開く
constexpr int MM_OPEN_TIMEOUT_MS = 2000;

// open が 0 を返したら開けた、それ以外は開けなかった。
// 開けたときの後始末（間に合わなかったとき用）を close に渡す。
// 戻り値: 0 開けた / 1 開けなかった / 2 時間切れ
template <typename Handle>
int open_with_timeout(std::function<unsigned(Handle &)> open,
                      std::function<void(Handle)> close, Handle &out)
{
	struct shared {
		std::mutex m;
		std::condition_variable cv;
		bool done = false, abandoned = false;
		unsigned rc = 1;
		Handle h{};
	};
	auto s = std::make_shared<shared>();

	std::thread([s, open, close] {
		Handle h{};
		const unsigned rc = open(h);
		std::unique_lock<std::mutex> lock(s->m);
		if (s->abandoned) {
			lock.unlock();
			if (rc == 0)
				close(h);              // もう誰も待っていない。掴んだまま残さない
			return;
		}
		s->rc = rc;
		s->h = h;
		s->done = true;
		s->cv.notify_all();
	}).detach();

	std::unique_lock<std::mutex> lock(s->m);
	if (!s->cv.wait_for(lock, std::chrono::milliseconds(MM_OPEN_TIMEOUT_MS),
	                    [&] { return s->done; })) {
		s->abandoned = true;
		return 2;
	}
	if (s->rc != 0)
		return 1;
	out = s->h;
	return 0;
}

// 閉じるときも同じで、固まった相手だと midiInReset や midiInClose が戻らない。
// 別の糸でやらせて、決めた時間だけ待つ。間に合わなければ置いていく
// （job は最後まで走り続けるので、job が使うものは job の中で持つこと）。
// 戻り値: 間に合ったか
inline bool run_with_timeout(std::function<void()> job)
{
	struct shared {
		std::mutex m;
		std::condition_variable cv;
		bool done = false;
	};
	auto s = std::make_shared<shared>();
	std::thread([s, job] {
		job();
		std::lock_guard<std::mutex> lock(s->m);
		s->done = true;
		s->cv.notify_all();
	}).detach();
	std::unique_lock<std::mutex> lock(s->m);
	return s->cv.wait_for(lock, std::chrono::milliseconds(MM_OPEN_TIMEOUT_MS),
	                      [&] { return s->done; });
}

} // namespace ui

#endif // S_MU2000_UI_MM_OPEN_H
