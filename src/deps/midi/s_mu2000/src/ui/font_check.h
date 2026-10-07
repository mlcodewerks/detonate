// license:BSD-3-Clause
//
// **その文字ファイルを、文字を描く部品（Dear ImGui の中の stb_truetype）が読めるか**を、渡す前に確かめる。
//
// 読めないファイルを渡すと、ImGui は「stbtt_InitFont(): failed to parse FontData」で assert して、プログラムごと
// 止まる（戻り値で断れない）。fontconfig が最初に答える日本語のファイルが読めない形のことがある:
// openSUSE Tumbleweed の Noto Sans CJK は可変フォント（CFF2）で、stb_truetype は CFF2 を読めない（issue #135）。
// だから、候補を 1 つずつここで確かめて、読めるものが出るまで次へ進む（font_file.h）。
//
// 見るのは stbtt_InitFont が要るとしているものと同じ:
//   ・sfnt の頭（TrueType・OpenType）か、その束（TTC）で、指した番号の書体があること
//   ・cmap・head・hhea・hmtx の表があること
//   ・輪郭が glyf（＋ loca）か CFF にあること（CFF2 だけ・ビットマップだけの書体は読めない）
//   ・cmap に、Unicode で引ける対応表があること
// 表の中身までは解かない。ここを通っても壊れたファイルは有り得るが、配られている書体で実際に起きるのは上の形

#ifndef S_MU2000_UI_FONT_CHECK_H
#define S_MU2000_UI_FONT_CHECK_H

#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

inline bool font_parses(const unsigned char *d, size_t n, int face = 0)
{
	auto u16 = [&](size_t o) { return uint32_t(d[o]) << 8 | d[o + 1]; };
	auto u32 = [&](size_t o) { return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3]; };
	auto tag = [&](size_t o, const char *t) { return d[o] == (unsigned char)t[0] && d[o + 1] == (unsigned char)t[1] &&
	                                                 d[o + 2] == (unsigned char)t[2] && d[o + 3] == (unsigned char)t[3]; };
	if (!d || n < 12 || face < 0)
		return false;
	// 束（TTC）なら、その番号の書体の頭へ
	size_t start = 0;
	if (tag(0, "ttcf")) {
		const uint32_t ver = u32(4);
		if ((ver != 0x00010000 && ver != 0x00020000) || uint32_t(face) >= u32(8) || n < 12 + size_t(face) * 4 + 4)
			return false;
		start = u32(12 + size_t(face) * 4);
	} else if (face != 0) {
		return false;
	}
	if (start + 12 > n)
		return false;
	const bool sfnt = (d[start] == '1' && d[start + 1] == 0 && d[start + 2] == 0 && d[start + 3] == 0) || tag(start, "typ1") ||
	                  tag(start, "OTTO") || (d[start] == 0 && d[start + 1] == 1 && d[start + 2] == 0 && d[start + 3] == 0) ||
	                  tag(start, "true");
	if (!sfnt)
		return false;
	const size_t tables = u16(start + 4);
	if (start + 12 + tables * 16 > n)
		return false;
	auto find = [&](const char *t) -> size_t {
		for (size_t i = 0; i < tables; i++) {
			const size_t e = start + 12 + i * 16;
			if (tag(e, t)) {
				const size_t off = u32(e + 8);
				return off && off < n ? off : 0;
			}
		}
		return 0;
	};
	const size_t cmap = find("cmap");
	if (!cmap || !find("head") || !find("hhea") || !find("hmtx"))
		return false;
	if (find("glyf")) {
		if (!find("loca"))
			return false;
	} else if (!find("CFF ")) {
		return false;                    // CFF2（可変フォント）だけ、ビットマップだけ、など
	}
	// Unicode で引ける対応表（Microsoft の Unicode BMP / 全域、または Unicode の面）
	if (cmap + 4 > n)
		return false;
	const size_t subs = u16(cmap + 2);
	if (cmap + 4 + subs * 8 > n)
		return false;
	for (size_t i = 0; i < subs; i++) {
		const size_t rec = cmap + 4 + i * 8;
		const uint32_t platform = u16(rec), encoding = u16(rec + 2);
		if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))
			return true;
	}
	return false;
}

} // namespace ui

#endif // S_MU2000_UI_FONT_CHECK_H
