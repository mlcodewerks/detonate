// license:BSD-3-Clause
//
// マスターの窓（doc/pc-editor.md）。一覧のマスターの行（MASTER の名前か MASTER EQ の絵）を
// ダブルクリックすると開く。一覧の小さなマスター EQ は見るだけなので、触るのはこの窓で。
//
//   上の左   システム: マスターボリューム、マスターチューン、移調
//   上の右   システムエフェクト: リバーブ・コーラス・バリエーションの種類、戻り量、パン、ほかへの送り、
//            バリエーションの接続と掛けるパート
//   下       マスター EQ: 種類、帯 1・5 の形、5 つの帯の特性（点をつまむ）と、帯ごとのゲイン・周波数・Q の棒
//
// 表示の大きさは xgui::master_zoom（既定 0.8）。窓の大きさもその分だけ小さくしてある

#ifndef S_MU2000_UI_MASTER_EDITOR_H
#define S_MU2000_UI_MASTER_EDITOR_H

#pragma once

#include "xg_ui.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace ui {

class master_editor : public imgui_view
{
public:
	const wchar_t *title() const override
	{
		return get_lang() == lang::ja ? L"S-MU2000 マスター" : L"S-MU2000 Master";
	}
	int default_width() const override  { return 900; }
	int default_height() const override { return 640; }
	void draw(xg::model &m, const xg_snapshot &ram, bridge &br) override;

private:
	// .syx の書き出し・読み込み（issue #35）
	void sysex_pane(const xg_snapshot &ram, bridge &br);
	// 架空のプラグインボード（src/vboard.h）。挿すボードと、挿すパート（1-64）
	void board_pane(bridge &br);
	void board_slot_pane(bridge &br, int slot, const std::string &kinds);
	// 挿さっているマルチパートのボードの種類（画面の控えから。無ければ 0）
	int board_multi_kind() const
	{
		for (int k : m_board_kind)
			if (mu2000::board_is_multi(k))
				return k;
		return 0;
	}
	void board_dls_pane(bridge &br);
	// マルチパートのボード（16 パートの FC ボード・DLS ボード）の 16 チャンネル: 音色・音量・パン・送り・メーター
	void board_parts_pane(xg::model &m, bridge &br);
	struct board_parts_info { std::mutex lock; mu2000::board_part part[16]; bool valid = false; };
	std::shared_ptr<board_parts_info> m_board_parts = std::make_shared<board_parts_info>();
	// 差込口（PLG-1〜3）ごとの控え
	int m_board_kind[mu2000::PLG_SLOTS] = { 0, 0, 0 }, m_board_part[mu2000::PLG_SLOTS] = { 1, 2, 3 };
	// 音源の側のいまの様子（音声の糸が置く）。下 8 ビットがパート（1-64、0 は MU のメニューで off）、bit8 が「MU がボードを見つけている」。
	// MU のメニュー（UTIL → PLG）でパートを変えられるので、ときどき聞いて欄を合わせる
	std::shared_ptr<std::atomic<int>> m_board_seen[mu2000::PLG_SLOTS] = {
		std::make_shared<std::atomic<int>>(-1), std::make_shared<std::atomic<int>>(-1), std::make_shared<std::atomic<int>>(-1) };
	double m_board_asked[mu2000::PLG_SLOTS] = { 0, 0, 0 }, m_board_touched[mu2000::PLG_SLOTS] = { -10, -10, -10 };
	// DLS のボードが読んでいるファイルの様子（音声の糸が置く）
	struct board_dls_info { std::mutex lock; std::string path, error; int instruments = 0, waves = 0; };
	std::shared_ptr<board_dls_info> m_board_dls = std::make_shared<board_dls_info>();
	char m_board_dls_input[512] = {};
	// オリジナルのボード（src/ui/user_boards.h）: 置き場のボードの並び
	void board_user_pane(bridge &br);
	std::vector<std::string> m_ub_list;
	double m_ub_listed = -1;
	std::string m_ub_note;
	bool m_board_booting[mu2000::PLG_SLOTS] = { false, false, false };   // 挿して本体を起動し直している（終わったらボードのバンクを選ぶ）
	bool m_diff_only = true;              // 既定と違うものだけ書き出す
	bool m_export_waiting = false;        // 既定値ができるのを待っている（bridge の request_defaults）
	std::vector<u8> m_import;             // 読み込んだ中身。1 通ずつ音源へ流す
	size_t m_import_at = 0;
	u64 m_import_hold_until = 0;          // XG System On などの後は少し待つ（音源の時計、ミリ秒）
};

} // namespace ui

#endif // S_MU2000_UI_MASTER_EDITOR_H
