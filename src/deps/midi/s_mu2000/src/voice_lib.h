// license:BSD-3-Clause
//
// **自作音色のライブラリ**の 1 件（ファイル 1 つ）。サンプル音色 1 つを、それだけで元に戻せる形で持つ:
//
//   ・音色の記録 350 バイト（要素 4 つの設定まるごと。doc/sampling-ram.md）
//   ・要素が鳴らしている**サンプル**（あれば）の波形と、鳴らす所・ループの設定・名前
//   ・覚え書き
//
// サンプリングのメモリは電源を切ると消える（実機は SmartMedia に残す）。作り込んだ音色を 1 つずつ PC に取っておき、
// 好きなものを選んで好きな枠へ戻せるようにするためのもの。内蔵ウェーブだけで組んだ音色は波形を持たないので
// 数百バイトで済む。
//
// サンプルは**番号ではなく中身で**持つ: 戻すときに、同じ波形がもうメモリにあればそれを使い、無ければ空きへ足して、
// 要素の指す番号を書き直す（mu2000::sampling_import_voice）。だから、別の日に別の順で録ったメモリへも戻せる。
//
// ファイルの形（数は little endian）:
//   "SMUVOICE" 8 バイト、u32 版（1）
//   u32 音色の記録の長さ（350）＋ その中身
//   u32 覚え書きの長さ ＋ UTF-8
//   4 × s32 要素 1-4 が鳴らすサンプル（下の並びの番号。サンプルでなければ -1）
//   u32 サンプルの数。1 つごとに: u32 名前の長さ ＋ 名前、u32 ループ（0/1）、u32 鳴り始め、u32 鳴り終わり、u32 ループの頭、
//   u32 サンプル数 ＋ s16 × サンプル数

#ifndef S_MU2000_VOICE_LIB_H
#define S_MU2000_VOICE_LIB_H

#pragma once

#include "compat/mamecompat.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace smu2000::voicelib {

constexpr u32 VERSION = 1;
constexpr u32 MAX_FRAMES = 0x200000;       // サンプリング RAM に入りきる長さ（4MB = 2M サンプル）

struct sample {
	std::string name;
	bool loop = false;
	u32 play_from = 0, play_to = 0, loop_from = 0;
	std::vector<s16> pcm;
};

struct item {
	std::vector<u8> voice;                  // 音色の記録 350 バイト
	std::string memo;
	std::array<int, 4> el_sample{ { -1, -1, -1, -1 } };   // 要素が鳴らすサンプル（samples の番号）。-1 = サンプルでない
	std::vector<sample> samples;

	// 音色の名前（記録の +2 から 8 文字。後ろの空白は落とす）
	std::string name() const
	{
		if (voice.size() < 10)
			return {};
		std::string s(reinterpret_cast<const char *>(&voice[2]), 8);
		while (!s.empty() && (s.back() == ' ' || s.back() == 0))
			s.pop_back();
		return s;
	}
	void set_name(const std::string &n)
	{
		if (voice.size() < 10)
			return;
		for (size_t i = 0; i < 8; i++)
			voice[2 + i] = i < n.size() && u8(n[i]) >= 0x20 && u8(n[i]) < 0x7f ? u8(n[i]) : u8(' ');
	}
	// 要素 e（0-3）を鳴らすか（記録の頭の印）
	bool el_on(int e) const { return !voice.empty() && (voice[0] >> e) & 1; }
	// 要素 e が鳴らす内蔵ウェーブ（0-502）。サンプルか、何も無ければ -1
	int el_rom_wave(int e) const
	{
		const size_t b = 12 + 84 * size_t(e);
		if (voice.size() < b + 4 || (voice[b + 2] & 0x40))
			return -1;
		const int set = (voice[b + 2] << 7) | (voice[b + 3] & 0x7f);
		return set < 503 ? set : -1;
	}
	size_t frames() const
	{
		size_t n = 0;
		for (const sample &s : samples)
			n += s.pcm.size();
		return n;
	}
};

inline std::vector<u8> save(const item &it)
{
	std::vector<u8> out;
	auto u32le = [&](u32 v) { for (int i = 0; i < 4; i++) out.push_back(u8(v >> (8 * i))); };
	auto bytes = [&](const void *p, size_t n) { const u8 *b = static_cast<const u8 *>(p); out.insert(out.end(), b, b + n); };
	bytes("SMUVOICE", 8);
	u32le(VERSION);
	u32le(u32(it.voice.size()));
	bytes(it.voice.data(), it.voice.size());
	u32le(u32(it.memo.size()));
	bytes(it.memo.data(), it.memo.size());
	for (int e : it.el_sample)
		u32le(u32(e));
	u32le(u32(it.samples.size()));
	for (const sample &s : it.samples) {
		u32le(u32(s.name.size()));
		bytes(s.name.data(), s.name.size());
		u32le(s.loop ? 1 : 0);
		u32le(s.play_from);
		u32le(s.play_to);
		u32le(s.loop_from);
		u32le(u32(s.pcm.size()));
		for (s16 v : s.pcm) {
			out.push_back(u8(v));
			out.push_back(u8(u16(v) >> 8));
		}
	}
	return out;
}

// 読む。形がおかしければ false（err に理由）。pcm を要らないとき（一覧を作るだけ）は with_pcm を偽に: 波形は読み飛ばし、
// 長さだけ合うように空のまま数を覚える（sample.pcm は空、frames_out に合計）
inline bool load(const std::vector<u8> &in, item &it, std::string &err, bool with_pcm = true, size_t *frames_out = nullptr)
{
	size_t at = 0;
	auto need = [&](size_t n) { return at + n <= in.size(); };
	auto u32le = [&](u32 &v) {
		if (!need(4))
			return false;
		v = u32(in[at]) | u32(in[at + 1]) << 8 | u32(in[at + 2]) << 16 | u32(in[at + 3]) << 24;
		at += 4;
		return true;
	};
	it = item();
	if (!need(8) || std::memcmp(in.data(), "SMUVOICE", 8)) {
		err = "not a voice file";
		return false;
	}
	at = 8;
	u32 ver = 0, n = 0;
	if (!u32le(ver) || ver != VERSION) {
		err = "unknown version";
		return false;
	}
	if (!u32le(n) || n != 350 || !need(n)) {
		err = "bad voice record";
		return false;
	}
	it.voice.assign(in.begin() + long(at), in.begin() + long(at + n));
	at += n;
	if (!u32le(n) || n > 65536 || !need(n)) {
		err = "bad memo";
		return false;
	}
	it.memo.assign(reinterpret_cast<const char *>(&in[at]), n);
	at += n;
	for (int &e : it.el_sample) {
		u32 v = 0;
		if (!u32le(v)) {
			err = "truncated";
			return false;
		}
		e = int(v);
	}
	u32 count = 0;
	if (!u32le(count) || count > 4) {
		err = "bad sample count";
		return false;
	}
	size_t total = 0;
	for (u32 i = 0; i < count; i++) {
		sample s;
		u32 loop = 0, frames = 0;
		if (!u32le(n) || n > 64 || !need(n)) {
			err = "bad sample name";
			return false;
		}
		s.name.assign(reinterpret_cast<const char *>(&in[at]), n);
		at += n;
		if (!u32le(loop) || !u32le(s.play_from) || !u32le(s.play_to) || !u32le(s.loop_from) || !u32le(frames) ||
		    frames > MAX_FRAMES || !need(size_t(frames) * 2)) {
			err = "bad sample";
			return false;
		}
		s.loop = loop != 0;
		total += frames;
		if (with_pcm) {
			s.pcm.resize(frames);
			for (u32 k = 0; k < frames; k++)
				s.pcm[k] = s16(u16(in[at + k * 2]) | u16(in[at + k * 2 + 1]) << 8);
		}
		at += size_t(frames) * 2;
		it.samples.push_back(std::move(s));
	}
	for (int &e : it.el_sample)
		if (e < -1 || e >= int(count)) {
			err = "bad element sample";
			return false;
		}
	if (frames_out)
		*frames_out = total;
	return true;
}

} // namespace smu2000::voicelib

#endif // S_MU2000_VOICE_LIB_H
