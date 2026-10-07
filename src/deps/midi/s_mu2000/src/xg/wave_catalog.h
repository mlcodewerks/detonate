// license:BSD-3-Clause
//
// 内蔵ウェーブ（波形の組 0-502）の一覧。組ごとに、
//   ・鍵の区切りごとの波形（長さ・ループ・もとの音程・形式）
//   ・それを使っている音色（名前と、選ぶときのバンク・プログラム）
//   ・それを使っているドラムの打（キットと鍵の名前）
//   ・絵（使っている音色の、実機の液晶に出る楽器の絵の番号）
// を ROM から引く。サンプリングの窓の「内蔵ウェーブ」用（何の音かを、使われ方から見当を付ける）。
//
// **波形に名前は無い**。ROM にあるのは番号だけで、ここで出すのは「使っている音色の名前」。
//
// ドラムの打は波形の組を指さず、波形の記録（16 バイト）を自分で持っている（native_voice.h の drum_record）。
// なので「波形 ROM の番地が同じ」ことで組に結び付ける。SFX キットの打は音色の記録を指すので、その要素の組。

#ifndef S_MU2000_XG_WAVE_CATALOG_H
#define S_MU2000_XG_WAVE_CATALOG_H

#pragma once

#include "native_voice.h"
#include "voices.h"

#include <map>
#include <string>
#include <vector>

namespace xg {

// 鍵の区切り 1 つぶんの波形。start・loop・address はレジスタ 0x12/13・0x14/15・0x16/17 に書く値
// （逆向きの入れ替えは済み）。mu2000::rom_wave_pcm にそのまま渡せる
struct wave_zone {
	int key_lo = 0, key_hi = 127;   // この波形を使う鍵
	int base_key = 60;              // もとの音程（この鍵で、録った速さのまま鳴る）
	int fine_cents = 0;
	int level = 0;                  // 減衰（0.375dB 目盛り）
	u32 start = 0, loop = 0, address = 0;
	u32 frames() const { return (start & 0xffffff) + (loop & 0xffffff); }
	u32 loop_at() const { return start & 0xffffff; }
	// くり返すか。逆向き（loop の bit31）と「くり返さない」の印（start の bit30）は 1 度きり。
	// 長い頭のあとの、ごく短いループ（打楽器の終わりで最後の値を持ち続けるだけ。Ntrl Kit のキックは 25765 サンプルのあとに 16）も
	// 1 度きりに数える。頭の無い短いループ（1 周期の波形）はくり返す
	bool loops() const
	{
		const u32 pre = start & 0xffffff, len = loop & 0xffffff;
		if ((loop & 0x80000000) || (start & 0x40000000) || len <= 8)
			return false;
		return !(len <= 64 && pre > 1000);
	}
	bool backwards() const { return (loop & 0x80000000) != 0; }
	int format() const { return int(address >> 30); }   // 0 = 16bit、1 = 12bit、2 = 8bit、3 = 圧縮
};

struct wave_voice_use {
	std::string name;
	int msb = -1, lsb = 0, prog = 0;   // 選び方（XG）。msb が -1 はバンクから引けない記録
};

struct wave_drum_use {
	std::string kit, key_name;
	int msb = 127, prog = 0, key = 0;   // msb -1 = TG300B（GS）モードのキット
	int more_kits = 0;                 // 同じ名前の打で、ほかにも使っているキットの数
};

struct wave_set_info {
	std::vector<wave_zone> zones;
	std::vector<wave_voice_use> voices;
	std::vector<wave_drum_use> drums;
	int icon = -1;                     // voice_rom::icon_rows に渡す番号。-1 = 無い
	bool used() const { return !voices.empty() || !drums.empty(); }
};

inline std::vector<wave_set_info> wave_catalog(const voice_rom &vr)
{
	constexpr int SETS = 503;
	std::vector<wave_set_info> out(SETS);
	const u8 *rom = vr.data();
	if (!rom)
		return out;

	// ---- 波形。組の並びは「鍵の上限」の順で、上限 0x7f が最後
	std::multimap<u32, int> by_addr;       // 波形 ROM の番地 → 組
	for (int set = 0; set < SETS; set++) {
		u32 s = nv::WAVE_BASE + nv::rd16(rom, nv::SET_TABLE + u32(set) * 2);
		int lo = 0;
		for (int i = 0; i < 80; i++, s += 16) {
			const nv::wave_info w = nv::read_wave(rom + s);
			wave_zone z;
			z.key_lo = lo;
			z.key_hi = w.key_max;
			z.base_key = w.base_key;
			z.fine_cents = w.fine_cents;
			z.level = w.level;
			u32 pre = w.pre_loop, loop = w.loop_len;
			nv::wave_backwards_swap(pre, loop);
			z.start = pre;
			z.loop = loop;
			z.address = w.format_addr;
			if (z.frames() > 0) {
				out[size_t(set)].zones.push_back(z);
				by_addr.emplace(z.address & 0x1ffffff, set);
			}
			lo = w.key_max + 1;
			if (w.key_max >= 0x7f)
				break;
		}
	}

	// ---- 音色。記録 → 選び方（最初に見つかったバンク。LSB 0 を先に当たる）
	std::map<u32, wave_voice_use> how;
	for (int msb : { 0, 64, 48, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
	                 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 49, 50, 51,
	                 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
	                 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101,
	                 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
	                 121, 122, 123, 124, 125 })
		for (int lsb = 0; lsb < 128; lsb++) {
			if (!vr.lsb_ok(1, 0, msb, lsb))
				continue;
			for (int prog = 0; prog < 128; prog++) {
				const u32 rec = vr.lookup(1, 0, msb, lsb, prog);
				if (!rec || how.count(rec))
					continue;
				wave_voice_use u;
				u.name = vr.record_name(rec);
				u.msb = msb;
				u.lsb = lsb;
				u.prog = prog;
				how.emplace(rec, std::move(u));
			}
		}
	// 記録を頭から順に。「14 + 84 × 要素の数」バイトずつ並ぶ
	auto add_voice = [&](int set, const wave_voice_use &u) {
		if (set < 0 || set >= SETS)
			return;
		wave_set_info &w = out[size_t(set)];
		for (const wave_voice_use &x : w.voices)
			if (x.name == u.name && x.msb == u.msb && x.lsb == u.lsb && x.prog == u.prog)
				return;
		// バンクから引ける音色を前に
		if (u.msb >= 0) {
			size_t at = 0;
			while (at < w.voices.size() && w.voices[at].msb >= 0)
				at++;
			w.voices.insert(w.voices.begin() + long(at), u);
		} else {
			w.voices.push_back(u);
		}
	};
	for (u32 a = nv::SFX_VOICES; a + 14 <= nv::SFX_VOICES_END;) {
		const int n = nv::element_count(rom, a);
		if (!n)
			break;
		wave_voice_use u;
		const auto it = how.find(a);
		if (it != how.end())
			u = it->second;
		else
			u.name = vr.record_name(a);
		for (int e = 0; e < n; e++)
			add_voice(nv::wave_set(rom + a + 12 + u32(e) * 84), u);
		a += 14 + 84 * u32(n);
	}

	// ---- ドラムの打
	auto add_drum = [&](int set, const wave_drum_use &d) {
		if (set < 0 || set >= SETS)
			return;
		for (wave_drum_use &x : out[size_t(set)].drums)
			if (x.key_name == d.key_name) {
				if (x.kit != d.kit)
					x.more_kits++;
				return;
			}
		out[size_t(set)].drums.push_back(d);
	};
	// 1 つのキットの全部の鍵。msb は 127・126（XG）か -1（TG300B）
	auto add_kit = [&](int kit, const std::string &kit_name, int msb, int prog) {
		for (int key = 0; key < 128; key++) {
			const u8 *rec = nv::drum_record(rom, kit, key);
			if (!rec)
				continue;
			wave_drum_use d;
			d.kit = kit_name;
			if (msb >= 0)
				d.key_name = vr.drum_key_name(msb, prog, key);
			if (d.key_name.empty())
				d.key_name = kit_name + " key " + std::to_string(key);
			d.msb = msb;
			d.prog = prog;
			d.key = key;
			if (nv::drum_rec_has_wave(rec)) {
				const u32 addr = nv::rd32(rec, 26 + 12) & 0x1ffffff;
				const auto range = by_addr.equal_range(addr);
				for (auto it = range.first; it != range.second; ++it)
					add_drum(it->second, d);
			} else if (const u32 vrec = nv::sfx_voice_record(rom, rec)) {
				const int n = nv::element_count(rom, vrec);
				for (int e = 0; e < n; e++)
					add_drum(nv::wave_set(rom + vrec + 12 + u32(e) * 84), d);
			}
		}
	};
	for (int msb : { 127, 126 })
		for (int prog = 0; prog < 128; prog++) {
			if (msb == 127 && prog == 126)
				continue;                    // プログラム 1 と同じキット
			const int kit = vr.kit_number(msb, prog);
			if (kit >= 0)
				add_kit(kit, vr.kit_name(msb, prog), msb, prog);
		}
	// TG300B（GS）モードのキット。XG のキットと同じ波形を使う打が多いが、こちらだけが使う組もある
	for (const voice_rom::tg_kit &k : voice_rom::TG300B_KITS)
		add_kit(k.kit, std::string("TG300B ") + k.name, -1, k.prog);

	// ---- 絵。バンクから引ける最初の音色のもの。音色が無くてドラムだけならドラムの絵
	for (wave_set_info &w : out) {
		for (const wave_voice_use &u : w.voices)
			if (u.msb >= 0) {
				w.icon = vr.icon_index(u.msb, u.prog);
				break;
			}
		if (w.icon < 0 && !w.drums.empty())
			w.icon = voice_rom::ICON_DRUM;
	}
	return out;
}

// 内蔵の音色 1 つ（ROM の音色の記録）。サンプリングの窓の「内蔵音色」用
struct preset_voice {
	u32 rec = 0;                       // 記録の番地（voice_rom::lookup の戻り値。mu2000::sampling_copy_preset に渡せる）
	std::string name;
	int msb = -1, lsb = 0, prog = 0;   // 選び方（XG）。msb が -1 はバンクから引けない記録
	int elements = 1;
	int icon = -1;
};

// 内蔵の音色の一覧。XG のバンクから引けるものを、番号 → MSB → LSB の順に（同じ番号の変種が並ぶ。効果音のバンク 64 は後ろ）。
// 同じ記録を指すバンクがいくつもあるときは、最初に見つかった選び方 1 つだけ。バンクから引けない記録はその後ろ
inline std::vector<preset_voice> preset_voices(const voice_rom &vr)
{
	std::vector<preset_voice> out;
	const u8 *rom = vr.data();
	if (!rom)
		return out;
	std::map<u32, size_t> seen;
	auto add = [&](u32 rec, int msb, int lsb, int prog) {
		if (!rec || seen.count(rec))
			return;
		preset_voice v;
		v.rec = rec;
		v.name = vr.record_name(rec);
		v.msb = msb;
		v.lsb = lsb;
		v.prog = prog;
		v.elements = nv::element_count(rom, rec);
		v.icon = msb >= 0 ? vr.icon_index(msb, prog) : -1;
		seen.emplace(rec, out.size());
		out.push_back(std::move(v));
	};
	for (int pass = 0; pass < 2; pass++)
		for (int prog = 0; prog < 128; prog++)
			for (int msb = 0; msb < 126; msb++) {
				if ((msb == 64) != (pass == 1))
					continue;
				for (int lsb = 0; lsb < 128; lsb++)
					if (vr.lsb_ok(1, 0, msb, lsb))
						add(vr.lookup(1, 0, msb, lsb, prog), msb, lsb, prog);
			}
	for (u32 a = nv::SFX_VOICES; a + 14 <= nv::SFX_VOICES_END;) {
		const int n = nv::element_count(rom, a);
		if (!n)
			break;
		add(a, -1, 0, 0);
		a += 14 + 84 * u32(n);
	}
	return out;
}

} // namespace xg

#endif // S_MU2000_XG_WAVE_CATALOG_H
