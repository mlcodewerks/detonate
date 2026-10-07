// license:BSD-3-Clause
//
// THRU（受けたものを外の MIDI 出力へ流す）の流量に上限を付ける。
//
// loopMIDI のような仮想の口には速さの上限が無い。DAW と loopMIDI で MIDI の輪が
// できると、メッセージが無限に増えて一瞬で何万バイトにもなる。それをそのまま
// THRU で実機へ流したら、**実機まで固まった**（Reason の MIDI Out Device を
// 足したとき）。本物の MIDI の線は 31250bps（1 秒に 3125 バイト）しか流れないので、
// その 2 倍を超えるぶんは捨てる。ふつうの演奏やダンプはこれに引っかからない。
//
// 捨てるときは**メッセージ単位**。途中のバイトだけ落とすと、受けた側で
// 別のメッセージに化ける。
//
// 音声スレッドだけが触る。数えた「捨てたバイト数」だけは画面の糸から読む。

#ifndef S_MU2000_UI_MIDI_GUARD_H
#define S_MU2000_UI_MIDI_GUARD_H

#pragma once

#include "compat/mamecompat.h"

#include <algorithm>
#include <atomic>

namespace ui {

class thru_guard
{
public:
	// 1 秒に流してよいバイト数（線の 2 倍）と、まとめて流してよい量
	static constexpr double RATE  = 3125.0 * 2;
	static constexpr double BURST = 16384.0;    // XG の一括ダンプが丸ごと入る

	// ブロックの頭で。進んだ時間ぶん、流してよい量を足す
	void refill(u32 frames, u32 sample_rate)
	{
		m_tokens = std::min(BURST, m_tokens + RATE * frames / sample_rate);
	}

	// 1 バイトずつ。流してよければ true
	bool pass(u8 b)
	{
		if (b >= 0xf8) {                        // リアルタイム。1 バイトで完結
			if (m_tokens >= 1.0) { m_tokens -= 1.0; return true; }
			drop();
			return false;
		}
		if ((b & 0x80) && b != 0xf7)            // メッセージの頭で、流すか捨てるかを決める
			m_dropping = m_tokens < 1.0;
		if (m_dropping) {
			drop();
			return false;
		}
		// 決めたメッセージは最後まで流す（残りが負になっても、次の頭で止まる）
		m_tokens -= 1.0;
		return true;
	}

	u64 dropped() const { return m_dropped.load(std::memory_order_relaxed); }

private:
	void drop() { m_dropped.fetch_add(1, std::memory_order_relaxed); }

	double m_tokens = BURST;
	bool   m_dropping = false;
	std::atomic<u64> m_dropped{0};
};

} // namespace ui

#endif // S_MU2000_UI_MIDI_GUARD_H
