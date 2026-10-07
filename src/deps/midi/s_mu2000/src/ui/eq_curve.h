// license:BSD-3-Clause
//
// EQ の特性の絵を描くための小物（一覧のパートの EQ・マスター EQ、エフェクトの設定の窓で共通）。
// 形は見た目の目安で、実際のフィルタの式ではない。周波数の軸は実際の周波数。

#ifndef S_MU2000_UI_EQ_CURVE_H
#define S_MU2000_UI_EQ_CURVE_H

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ui {
namespace eq {

// XG の EQ の周波数の表（値 0-60 → Hz）
inline constexpr int HZ[61] = {
	20, 22, 25, 28, 32, 36, 40, 45, 50, 56, 63, 70, 80, 90, 100, 110, 125, 140, 160, 180,
	200, 225, 250, 280, 315, 355, 400, 450, 500, 560, 630, 700, 800, 900, 1000, 1100, 1200, 1400, 1600, 1800,
	2000, 2200, 2500, 2800, 3200, 3600, 4000, 4500, 5000, 5600, 6300, 7000, 8000, 9000, 10000, 11000, 12000, 14000, 16000, 18000,
	20000 };

inline std::string hz_text(int index)
{
	const int hz = HZ[std::clamp(index, 0, 60)];
	char buf[16];
	if (hz >= 1000) std::snprintf(buf, sizeof(buf), hz % 1000 ? "%.1fk" : "%.0fk", hz / 1000.0);
	else            std::snprintf(buf, sizeof(buf), "%d", hz);
	return buf;
}

// 周波数の軸。20Hz-20kHz を対数で並べる。t は 0-1
inline float t_of_hz(float hz) { return std::log10(hz / 20.0f) / 3.0f; }
inline float hz_of_t(float t)  { return 20.0f * std::pow(10.0f, t * 3.0f); }

// 周波数の表の値のうち、横の位置 t に一番近いもの（lo-hi の中で）
inline int index_near(float t, int lo, int hi)
{
	lo = std::clamp(lo, 0, 60);
	hi = std::clamp(hi, lo, 60);
	int best = lo;
	for (int i = lo; i <= hi; i++)
		if (std::fabs(t_of_hz(float(HZ[i])) - t) < std::fabs(t_of_hz(float(HZ[best])) - t))
			best = i;
	return best;
}

// 1 帯の、周波数 f での持ち上がり（dB）
enum class shape { low_shelf, high_shelf, peak };
inline float band_db(shape s, float gain_db, float fc, float q, float f)
{
	const float r = f / fc;
	switch (s) {
	case shape::low_shelf:  return gain_db / (1.0f + r * r);
	case shape::high_shelf: return gain_db * r * r / (1.0f + r * r);
	default: {
		const float x = q * (r - 1.0f / r);
		return gain_db / (1.0f + x * x);
	}
	}
}

} // namespace eq
} // namespace ui

#endif // S_MU2000_UI_EQ_CURVE_H
