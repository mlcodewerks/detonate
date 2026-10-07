// license:BSD-3-Clause
//
// **オリジナルのボード**（架空のプラグインボードの 1 つ。src/vboard.h）。使う人が「波形を作る」（src/wavegen.h）で
// 計算した波形を、プログラム番号 1-128 に 1 つずつ割り当てて作る、自分だけのボード。
//
//   ・入るのは wavegen.h が計算で作った波形だけ。MU2000 の ROM の波形（内蔵ウェーブ）も、録音したサンプルも入れない
//     （画面にもその道は無い）。だからボードのファイルは、人に渡しても差し支えない
//   ・プログラム 1 つ = 名前 8 文字・波形（44.1kHz・モノラル・16bit。鍵 60 でそのままの高さ）・ループの有無・
//     音量の包絡線（アタック・ディケイ・サステイン・リリース）
//   ・音源は 16 チャンネル・32 声。鍵の高さに合わせて読む速さを変える（直線補間）。ピッチベンド（±2 半音）、
//     モジュレーション（CC1。ビブラート）、サステインペダル（CC64）、CC120 / CC123 / CC121
//   ・ファイル（.smuboard）は下の write_user_board / read_user_board の形

#ifndef S_MU2000_VBOARD_USER_H
#define S_MU2000_VBOARD_USER_H

#pragma once

#include "compat/mamecompat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace smu2000::vboard {

struct user_program {
	char name[9] = "        ";                        // 8 文字（足りない所は空白）
	std::shared_ptr<const std::vector<s16>> pcm;      // 波形。ループするものは全体をくり返す
	bool loop = true;
	float attack = 0.002f, decay = 0.0f, sustain = 1.0f, release = 0.08f;    // 秒・秒・0-1・秒（decay 0 = 下がらない）
};

struct user_board {
	static constexpr int PROGRAMS = 128;
	static constexpr u32 MAX_FRAMES = 44100 * 8;      // プログラム 1 つの波形の長さの上限
	char name[15] = "MY BOARD";                       // 14 文字まで（本体の UTIL → PLG に出る）
	std::array<std::shared_ptr<const user_program>, PROGRAMS> program;
	int count() const
	{
		int n = 0;
		for (const auto &p : program)
			n += p != nullptr;
		return n;
	}
};

// ---- ファイル。全部リトルエンディアン
//   "SMUBOARD"  u32 版（1）  名前 16 バイト  u32 プログラムの数
//   プログラムごとに: u8 番号（0-127）  u8 印（bit0 = ループ）  名前 8 バイト
//                     f32 アタック・ディケイ・サステイン・リリース  u32 サンプル数  s16 × サンプル数

namespace detail {
inline void put32(std::vector<u8> &o, u32 v)
{
	for (int i = 0; i < 4; i++)
		o.push_back(u8(v >> (8 * i)));
}
inline void putf(std::vector<u8> &o, float f)
{
	u32 v;
	std::memcpy(&v, &f, 4);
	put32(o, v);
}
inline u32 get32(const u8 *p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
inline float getf(const u8 *p, float lo, float hi, float fallback)
{
	const u32 v = get32(p);
	float f;
	std::memcpy(&f, &v, 4);
	return f >= lo && f <= hi ? f : fallback;       // NaN もここで落ちる
}
// 名前は表示できる ASCII だけ（本体の液晶に出すので）
inline void clean_name(char *dst, const char *src, size_t n, bool pad)
{
	size_t i = 0;
	for (; i < n && src[i]; i++)
		dst[i] = src[i] >= 0x20 && src[i] < 0x7f ? src[i] : '?';
	const size_t end = i;
	for (; i < n; i++)
		dst[i] = ' ';
	dst[pad ? n : end] = 0;
}
} // namespace detail

inline std::vector<u8> write_user_board(const user_board &b)
{
	std::vector<u8> o = { 'S', 'M', 'U', 'B', 'O', 'A', 'R', 'D' };
	detail::put32(o, 1);
	char name[16] = {};
	std::snprintf(name, sizeof(name), "%s", b.name);
	o.insert(o.end(), name, name + 16);
	detail::put32(o, u32(b.count()));
	for (int i = 0; i < user_board::PROGRAMS; i++) {
		const user_program *p = b.program[size_t(i)].get();
		if (!p)
			continue;
		o.push_back(u8(i));
		o.push_back(p->loop ? 1 : 0);
		o.insert(o.end(), p->name, p->name + 8);
		detail::putf(o, p->attack);
		detail::putf(o, p->decay);
		detail::putf(o, p->sustain);
		detail::putf(o, p->release);
		const std::vector<s16> empty;
		const std::vector<s16> &pcm = p->pcm ? *p->pcm : empty;
		detail::put32(o, u32(pcm.size()));
		for (s16 s : pcm) {
			o.push_back(u8(s));
			o.push_back(u8(u16(s) >> 8));
		}
	}
	return o;
}

inline std::shared_ptr<user_board> read_user_board(const u8 *d, size_t n, std::string &err)
{
	if (n < 32 || std::memcmp(d, "SMUBOARD", 8) != 0) {
		err = "not a board file";
		return nullptr;
	}
	if (detail::get32(d + 8) != 1) {
		err = "unknown version";
		return nullptr;
	}
	auto b = std::make_shared<user_board>();
	char name[15] = {};
	std::memcpy(name, d + 12, 14);
	detail::clean_name(b->name, name, 14, false);
	const u32 count = detail::get32(d + 28);
	size_t at = 32;
	for (u32 k = 0; k < count; k++) {
		if (n - at < 30) {
			err = "file is cut short";
			return nullptr;
		}
		const u8 number = d[at], flags = d[at + 1];
		auto p = std::make_shared<user_program>();
		char pn[9] = {};
		std::memcpy(pn, d + at + 2, 8);
		detail::clean_name(p->name, pn, 8, true);
		p->loop = flags & 1;
		p->attack = detail::getf(d + at + 10, 0.0f, 10.0f, 0.002f);
		p->decay = detail::getf(d + at + 14, 0.0f, 30.0f, 0.0f);
		p->sustain = detail::getf(d + at + 18, 0.0f, 1.0f, 1.0f);
		p->release = detail::getf(d + at + 22, 0.0f, 30.0f, 0.08f);
		const u32 frames = detail::get32(d + at + 26);
		at += 30;
		if (number >= user_board::PROGRAMS || frames > user_board::MAX_FRAMES || (n - at) / 2 < frames) {
			err = "file is cut short";
			return nullptr;
		}
		auto pcm = std::make_shared<std::vector<s16>>(frames);
		for (u32 i = 0; i < frames; i++)
			(*pcm)[i] = s16(u16(d[at + 2 * i]) | u16(d[at + 2 * i + 1]) << 8);
		at += size_t(frames) * 2;
		p->pcm = std::move(pcm);
		b->program[number] = std::move(p);
	}
	return b;
}

inline bool save_user_board(const std::string &path, const user_board &b)
{
	const std::vector<u8> bytes = write_user_board(b);
	std::FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	return std::fclose(f) == 0 && ok;
}

inline std::shared_ptr<user_board> load_user_board(const std::string &path, std::string &err)
{
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) {
		err = "cannot open the file";
		return nullptr;
	}
	std::vector<u8> bytes;
	u8 buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && bytes.size() < (128u << 20))
		bytes.insert(bytes.end(), buf, buf + n);
	std::fclose(f);
	return read_user_board(bytes.data(), bytes.size(), err);
}

// ---- 音源

class user_synth
{
public:
	static constexpr int VOICES = 32;
	static constexpr double RATE = 44100.0;
	static constexpr float QUICK = 1.0f / 441.0f;     // 10 ミリ秒で上がりきる、1 サンプルごとの上がり幅

	void set_board(std::shared_ptr<const user_board> b)
	{
		// 鳴っている声は自分のプログラム（波形）を持っているので、そのまま鳴り終わる
		m_board = std::move(b);
	}
	const user_board *board() const { return m_board.get(); }

	void reset()
	{
		for (voice &v : m_v)
			v.on = false;
		for (chan &c : m_c)
			c = chan();
	}

	u8 program(int channel) const { return m_c[size_t(channel & 15)].program; }
	// そのプログラムの名前（8 文字）。空いている番号は "--------"
	const char *program_name(int program) const
	{
		const user_program *p = m_board ? m_board->program[size_t(program & 127)].get() : nullptr;
		return p ? p->name : "--------";
	}

	void midi(u8 status, u8 d0, u8 d1)
	{
		const int ch = status & 15;
		chan &c = m_c[size_t(ch)];
		switch (status & 0xf0) {
		case 0x90:
			if (d1) {
				note_on(ch, d0, d1);
				break;
			}
			[[fallthrough]];
		case 0x80:
			for (voice &v : m_v)
				if (v.on && v.ch == ch && v.key == d0 && !v.released) {
					if (c.pedal)
						v.held = true;
					else
						v.released = true;
				}
			break;
		case 0xb0:
			switch (d0) {
			case 1:   c.mod = d1 / 127.0; break;
			case 64:
				c.pedal = d1 >= 64;
				if (!c.pedal)
					for (voice &v : m_v)
						if (v.on && v.ch == ch && v.held) {
							v.held = false;
							v.released = true;
						}
				break;
			case 120:
				for (voice &v : m_v)
					if (v.ch == ch)
						v.on = false;
				break;
			case 121:
				c.mod = 0.0;
				c.bend = 0.0;
				c.pedal = false;
				[[fallthrough]];
			case 123:
				for (voice &v : m_v)
					if (v.on && v.ch == ch && (d0 == 123 || v.held)) {
						v.held = false;
						v.released = true;
					}
				break;
			default: break;
			}
			break;
		case 0xc0:
			c.program = d0 & 127;
			break;
		case 0xe0:
			c.bend = (((d1 << 7) | d0) - 8192) / 8192.0 * 2.0;      // 半音
			break;
		default:
			break;
		}
	}

	bool sounding() const
	{
		for (const voice &v : m_v)
			if (v.on)
				return true;
		return false;
	}

	// 1 サンプル（44.1kHz）。チャンネルごとの音を足し込む（モノラル）。±1.0 が全振幅で、1 声の最大はおよそ 0.25
	void render(float out[16])
	{
		m_lfo += 5.5 / RATE;
		if (m_lfo >= 1.0)
			m_lfo -= 1.0;
		const double vib = std::sin(m_lfo * 6.283185307179586) * 0.5;       // ±0.5 半音まで
		// 高さの倍率は 16 サンプルごとに直す
		const bool retune = !(m_tick++ & 15);
		for (voice &v : m_v) {
			if (!v.on)
				continue;
			const std::vector<s16> &pcm = *v.pcm;
			const chan &c = m_c[size_t(v.ch)];
			if (retune || v.step == 0.0)
				v.step = std::pow(2.0, (double(v.key) - 60.0 + c.bend + c.mod * vib) / 12.0);
			const size_t n = pcm.size();
			size_t i = size_t(v.pos);
			if (i >= n) {
				v.on = false;
				continue;
			}
			const float f = float(v.pos - double(i));
			const float a = pcm[i];
			const float b = i + 1 < n ? pcm[i + 1] : v.prog->loop ? pcm[0] : 0.0f;
			out[v.ch] += (a + (b - a) * f) * (1.0f / 32768.0f) * v.env * v.gain;
			v.pos += v.step;
			if (v.pos >= double(n)) {
				if (v.prog->loop)
					v.pos = std::fmod(v.pos, double(n));
				else
					v.on = false;
			}
			// 包絡線: 上がる（直線）→ サステインへ近づく（指数）→ 離したら 0 へ（指数）
			// 速い立ち上がり（10 ミリ秒まで）は、途中で離されても上がりきってから下げる。そうしないと、
			// ノートオンのすぐ後にノートオフが来る音（ゲートの短いドラム）が鳴らずに消える
			if (v.released && !(v.rising && v.d_attack >= QUICK)) {
				v.env *= v.k_release;
				if (v.env < 1e-4f)
					v.on = false;
			} else if (v.rising) {
				v.env += v.d_attack;
				if (v.env >= 1.0f) {
					v.env = 1.0f;
					v.rising = false;
				}
			} else if (v.k_decay < 1.0f) {
				v.env = v.sustain + (v.env - v.sustain) * v.k_decay;
				if (v.env < 1e-4f)
					v.on = false;
			}
		}
	}

private:
	struct chan {
		u8 program = 0;
		double bend = 0.0, mod = 0.0;
		bool pedal = false;
	};
	struct voice {
		bool on = false, released = false, held = false, rising = true;
		int ch = 0;
		u8 key = 60;
		std::shared_ptr<const user_program> prog;      // 鳴っている間、波形を持っておく
		const std::vector<s16> *pcm = nullptr;
		double pos = 0.0, step = 0.0;
		float env = 0.0f, gain = 0.0f, sustain = 1.0f;
		float d_attack = 1.0f, k_decay = 1.0f, k_release = 0.0f;
		u32 age = 0;
	};

	// 時定数 t 秒で 60dB 下がる、1 サンプルごとの倍率
	static float fall(float seconds) { return seconds <= 0.0f ? 0.0f : float(std::pow(0.001, 1.0 / (double(seconds) * RATE))); }

	void note_on(int ch, u8 key, u8 vel)
	{
		if (!m_board)
			return;
		std::shared_ptr<const user_program> p = m_board->program[m_c[size_t(ch)].program];
		if (!p || !p->pcm || p->pcm->empty())
			return;
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
		use->ch = ch;
		use->key = key;
		use->pcm = p->pcm.get();
		const float v = vel / 127.0f;
		use->gain = 0.25f * v * v;
		use->sustain = p->sustain;
		use->d_attack = p->attack <= 0.0f ? 1.0f : float(1.0 / (double(p->attack) * RATE));
		use->k_decay = p->decay <= 0.0f || p->sustain >= 1.0f ? 1.0f : fall(p->decay);
		use->k_release = fall(std::max(p->release, 0.003f));
		use->prog = std::move(p);
		use->age = ++m_age;
	}

	std::shared_ptr<const user_board> m_board;
	std::array<voice, VOICES> m_v{};
	std::array<chan, 16> m_c{};
	double m_lfo = 0.0;
	u32 m_tick = 0, m_age = 0;
};

} // namespace smu2000::vboard

#endif // S_MU2000_VBOARD_USER_H
