// license:BSD-3-Clause
//
// マルチパートの架空のボード（16 パートの FC ボード・DLS ボード）の 16 チャンネルを、画面から読む・動かす。
// 本体（firmware）はこの種類のボードのパートを知らないので、本体のメモリーを読む一覧や音色の窓には出てこない。
// ここが音声の糸に様子を聞き（mu2000::board_parts）、動かした値は口 E へ MIDI で送る。
// マスターの窓の「ボードのパート」と、一覧の下の 16 行が使う

#ifndef S_MU2000_UI_BOARD_VIEW_H
#define S_MU2000_UI_BOARD_VIEW_H

#pragma once

#include "bridge.h"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <vector>

namespace ui {

class board_view
{
public:
	// 1 コマに 1 度。音声の糸にいまの様子を置いてもらう
	void poll(bridge &br)
	{
		br.post([info = m_info](mu2000 &mu) {
			mu2000::board_part now[16];
			const int kind = mu.virtual_board_multi();
			if (kind)
				mu.board_parts(now);
			std::lock_guard<std::mutex> g(info->lock);
			std::copy(std::begin(now), std::end(now), std::begin(info->part));
			info->kind = kind;
			return std::string();
		});
	}
	// 挿さっているマルチパートのボードの種類（mu2000::VBOARD_FC16 / VBOARD_DLS。無ければ 0）と、16 チャンネル
	int get(mu2000::board_part out[16]) const
	{
		std::lock_guard<std::mutex> g(m_info->lock);
		std::copy(std::begin(m_info->part), std::end(m_info->part), out);
		return m_info->kind;
	}
	// ボードで選べる音色の並びを聞く（品書きを開くとき）。答えは次のコマから voices で読める
	void ask_voices(bridge &br)
	{
		br.post([info = m_info](mu2000 &mu) {
			std::vector<mu2000::board_voice> v = mu.board_voices();
			std::lock_guard<std::mutex> g(info->lock);
			info->voices = std::move(v);
			return std::string();
		});
	}
	std::vector<mu2000::board_voice> voices() const
	{
		std::lock_guard<std::mutex> g(m_info->lock);
		return m_info->voices;
	}
	// 口 E へ MIDI を送る
	static void send(bridge &br, std::initializer_list<int> bytes)
	{
		u8 b[16];
		size_t n = 0;
		for (int x : bytes)
			if (n < sizeof(b))
				b[n++] = u8(x);
		br.send_port(mu2000::MIDI_PORTS, b, n);
	}
	// チャンネルの音をインサーションへ通す（0 = 通さない、1-4、5 = バリエーション）
	static void set_insert(bridge &br, int channel, int slot)
	{
		br.post([channel, slot](mu2000 &mu) {
			mu.set_board_insert(channel, slot);
			return std::string();
		});
	}

private:
	struct info { std::mutex lock; mu2000::board_part part[16]; int kind = 0; std::vector<mu2000::board_voice> voices; };
	std::shared_ptr<info> m_info = std::make_shared<info>();
};

} // namespace ui

#endif // S_MU2000_UI_BOARD_VIEW_H
