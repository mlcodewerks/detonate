// license:BSD-3-Clause
//
// **架空のプラグインボード**。実在しないボードを MU2000 に挿したことにして、その音を MU のミキサーとエフェクトに通す。
//
// 実機のプラグインボード（PLG100 / PLG150）は、割り当てたパートの MIDI を受けて自分で音を作り、その音を本体の
// DSP に渡す。本体はそのパートの音量・パン・エフェクトの送りを、内蔵の音色と同じように効かせる。ここも同じ形にする:
//   ・ボードは、選んだパートが受ける MIDI（そのパートの受信チャンネル）で鳴る
//   ・本体の内蔵の音は、そのパートだけ消す（mu2000 の中のパートのミュート）
//   ・ボードの音は、そのパートの音量・エクスプレッション・パン・リバーブ／コーラスの送りを掛けて、
//     MU のエフェクトの入口へ入れる（mu2000::set_external_audio）
// firmware はボードを知らない（液晶に PLG の印は出ない）。ボードと本体のやり取りを再現するのは別の話。
//
// 1 枚目は **FC ボード**: 80 年代の家庭用ゲーム機の音源（矩形波 2・三角波・ノイズ）ふうの音を、8 音まで重ねて鳴らす。
// 中身は全部ここで書いた計算で、ROM もサンプルも使わない。
//
//   プログラム（下 3 ビットが音、bit3 が鳴り方）
//     0 矩形波 50%   1 矩形波 25%   2 矩形波 12.5%   3 三角波（16 段）
//     4 ノイズ（長い周期）   5 ノイズ（短い周期。金属的）   6 デューティを 60 分の 1 秒ごとに切り替える矩形波
//     7 1 オクターブ上と交互に鳴る矩形波（高速アルペジオ）
//     +8 すると、押している間も音量が段々に下がる（減衰する鳴り方）
//   音量は 16 段（強さで決まる）、60 分の 1 秒ごとに段を進める。三角波は実物どおり音量を持たない（鳴るか鳴らないか）
//   ピッチベンド（±2 半音）、モジュレーション（CC1。ビブラート）、CC123 / CC120（全部止める）

#ifndef S_MU2000_VBOARD_H
#define S_MU2000_VBOARD_H

#pragma once

#include "compat/mamecompat.h"

#include <array>
#include <cmath>

namespace smu2000::vboard {

class fc_board
{
public:
	static constexpr int VOICES = 8;
	static constexpr double RATE = 44100.0;
	static constexpr int OVER = 4;               // 1 サンプルを 4 つに割って作り、平均する（折り返しを減らす）

	void reset()
	{
		for (voice &v : m_v)
			v = voice();
		m_program = 0;
		m_bend = 0.0;
		m_mod = 0.0;
		m_frame = 0.0;
		m_lfo = 0.0;
		m_age = 0;
	}

	// チャンネルメッセージ 1 つ（status は上 4 ビットだけ見る）
	void midi(u8 status, u8 d0, u8 d1)
	{
		switch (status & 0xf0) {
		case 0x90:
			if (d1) {
				note_on(d0, d1);
				break;
			}
			[[fallthrough]];
		case 0x80:
			for (voice &v : m_v)
				if (v.on && v.key == d0 && !v.released) {
					v.released = true;
					v.frames = 0;
				}
			break;
		case 0xb0:
			if (d0 == 1)
				m_mod = d1 / 127.0;
			else if (d0 == 120)
				for (voice &v : m_v)
					v.on = false;
			else if (d0 == 123)
				for (voice &v : m_v)
					if (v.on && !v.released) {
						v.released = true;
						v.frames = 0;
					}
			break;
		case 0xc0:
			m_program = d0 & 15;
			break;
		case 0xe0:
			m_bend = (((d1 << 7) | d0) - 8192) / 8192.0 * 2.0;      // 半音
			break;
		default:
			break;
		}
	}

	u8 program() const { return m_program; }

	// 鳴っている声があるか（無ければ本体は入口を空にできる）
	bool sounding() const
	{
		for (const voice &v : m_v)
			if (v.on)
				return true;
		return false;
	}

	// 1 サンプル（44.1kHz）。±1.0 が全振幅で、1 声の最大はおよそ 0.14
	float render()
	{
		// 60 分の 1 秒ごとに、音量の段・デューティ・アルペジオを進める
		m_frame += 60.0 / RATE;
		const bool tick = m_frame >= 1.0;
		if (tick)
			m_frame -= 1.0;
		m_lfo += 6.0 / RATE;
		if (m_lfo >= 1.0)
			m_lfo -= 1.0;
		const double vib = m_mod * 0.5 * std::sin(m_lfo * 6.283185307179586);    // ±0.5 半音まで
		double sum = 0.0;
		for (voice &v : m_v) {
			if (!v.on)
				continue;
			if (tick)
				step_frame(v);
			if (!v.on)
				continue;
			const int timbre = v.program & 7;
			double semis = double(v.key) + m_bend + vib;
			if (timbre == 7 && (v.frames & 1))
				semis += 12.0;
			const double freq = 440.0 * std::pow(2.0, (semis - 69.0) / 12.0);
			double out = 0.0;
			if (timbre == 4 || timbre == 5) {
				// ノイズ: 15 ビットの帰還シフトレジスタを、鍵の高さに合わせた速さで回す
				const double step = std::min(freq * 16.0 / (RATE * OVER), 1.0);
				for (int k = 0; k < OVER; k++) {
					v.phase += step;
					if (v.phase >= 1.0) {
						v.phase -= 1.0;
						const u32 tap = timbre == 5 ? 6 : 1;
						const u32 fb = (v.lfsr ^ (v.lfsr >> tap)) & 1;
						v.lfsr = (v.lfsr >> 1) | (fb << 14);
					}
					out += (v.lfsr & 1) ? 1.0 : -1.0;
				}
			} else {
				const double step = freq / (RATE * OVER);
				static constexpr double DUTY[4] = { 0.5, 0.25, 0.125, 0.25 };
				double duty = DUTY[timbre & 3];
				if (timbre == 6) {
					static constexpr double SWEEP[4] = { 0.125, 0.25, 0.5, 0.25 };
					duty = SWEEP[(v.frames >> 1) & 3];
				} else if (timbre == 7) {
					duty = 0.5;
				}
				for (int k = 0; k < OVER; k++) {
					v.phase += step;
					if (v.phase >= 1.0)
						v.phase -= 1.0;
					if (timbre == 3) {
						// 三角波: 16 段の階段（0-15-0）
						const int s = int(v.phase * 32.0) & 31;
						const int lvl = s < 16 ? s : 31 - s;
						out += lvl / 7.5 - 1.0;
					} else {
						out += v.phase < duty ? 1.0 : -1.0;
					}
				}
			}
			out /= OVER;
			// 三角波は音量を持たない（実物と同じ）。ほかは 16 段
			const double level = timbre == 3 ? (v.volume > 0 ? 1.0 : 0.0) : v.volume / 15.0;
			sum += out * level * 0.14;
		}
		return float(sum);
	}

private:
	struct voice {
		bool on = false, released = false;
		u8 key = 60, program = 0;
		int volume = 0;         // 0-15
		int frames = 0;         // 60 分の 1 秒の数（押してから、または離してから）
		double phase = 0.0;
		u32 lfsr = 1;
		u32 age = 0;
	};

	void note_on(u8 key, u8 vel)
	{
		// 空いている声、無ければ離している声、それも無ければいちばん古い声
		voice *use = nullptr;
		for (voice &v : m_v)
			if (!v.on) {
				use = &v;
				break;
			}
		if (!use)
			for (voice &v : m_v)
				if (v.released && (!use || v.age < use->age))
					use = &v;
		if (!use)
			for (voice &v : m_v)
				if (!use || v.age < use->age)
					use = &v;
		*use = voice();
		use->on = true;
		use->key = key;
		use->program = m_program;
		use->volume = 1 + (vel * 14 + 63) / 127;       // 1-15
		use->age = ++m_age;
	}

	// 60 分の 1 秒ぶん。離した声は 1 コマに 2 段ずつ下がって消える（0.13 秒以内）。減衰する鳴り方は、押している間も 4 コマごとに 1 段
	void step_frame(voice &v)
	{
		v.frames++;
		if (v.released) {
			v.volume -= 2;
			if (v.volume <= 0)
				v.on = false;
		} else if ((v.program & 8) && !(v.frames & 3)) {
			if (v.volume > 0 && --v.volume == 0)
				v.on = false;
		}
	}

	std::array<voice, VOICES> m_v{};
	u8 m_program = 0;
	double m_bend = 0.0, m_mod = 0.0, m_frame = 0.0, m_lfo = 0.0;
	u32 m_age = 0;
};

// FC ボードのプログラム（0-15）の名前（8 文字。液晶と画面に出す）
inline const char *fc_program_name(int program)
{
	static const char *const names[16] = {
		"Square50", "Square25", "Square12", "Triangle", "Noise   ", "MetalNz ", "DutySwp ", "OctArp  ",
		"Sq50 Dcy", "Sq25 Dcy", "Sq12 Dcy", "Tri Dcy ", "NoiseDcy", "MetalDcy", "SweepDcy", "ArpDcy  ",
	};
	return names[program & 15];
}

// パートの設定（MIDI の 0-127）を、ボードの音に掛ける倍率にする。本体の内蔵の音色と同じ「2 乗」の曲線（40log）
inline float level_of(int value)
{
	const float v = float(value < 0 ? 0 : value > 127 ? 127 : value) / 127.0f;
	return v * v;
}

// パン（1-127、64 が真ん中。0 はランダムなので真ん中として扱う）を左右の倍率に。真ん中で両方 1.0
inline void pan_of(int pan, float &left, float &right)
{
	const float p = pan <= 0 ? 0.5f : float(pan - 1) / 126.0f;       // 0 = 左、1 = 右
	left = std::min(1.0f, 2.0f * (1.0f - p));
	right = std::min(1.0f, 2.0f * p);
}

} // namespace smu2000::vboard

#endif // S_MU2000_VBOARD_H
