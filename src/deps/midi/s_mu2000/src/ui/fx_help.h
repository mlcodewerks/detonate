// license:BSD-3-Clause
//
// エフェクトの種類とパラメータの説明（fx_help.cpp）。言語は説明の設定（xg_ui.h）に従う。

#ifndef S_MU2000_UI_FX_HELP_H
#define S_MU2000_UI_FX_HELP_H

#pragma once

namespace ui {
namespace xgui {

// 種類の説明。LSB ごとの文が無ければ同じ MSB の文。無ければ nullptr
const char *fx_type_help(int msb, int lsb);
// パラメータの説明。名前は LCD の名前（xg/fx_params.h の label）。無ければ nullptr
const char *fx_param_help(const char *label);

} // namespace xgui
} // namespace ui

#endif // S_MU2000_UI_FX_HELP_H
