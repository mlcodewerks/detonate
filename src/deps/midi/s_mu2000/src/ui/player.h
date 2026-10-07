// license:BSD-3-Clause
//
// MIDI ファイルを実時間で音源へ流す。画面の「MIDI プレイヤー」用（イシュー #124）。
// 曲の一覧（プレイリスト）を持ち、一時停止・頭出し（シーク）・前後の曲・くり返しができる。
//
// **ここだけは時計を持つ**。譜面を送る側は時計を持つのが当たり前で、
// 実機に MIDI ケーブルで繋いだ外の並べ機と同じ立場になる。音源のほうは
// 今までどおり、音声デバイスに頼まれた分だけ進む（doc/design.md）。
//
// 送り先は ui::bridge の輪。エディタのつまみと同じ道なので、
// VST3 でもそのまま動く。
//
// 頭出しは、行き先までの設定（SysEx・音色・つまみ）を追いかけてから鳴らし始める（smf::chase）。
// 送ったものを音源が読み終えるまで待つので（USB の口で 1 秒に 10,000 バイト、DIN で 3,125 バイト）、
// SysEx の多い曲は少し待つ。その間は「追いかけ中」。

#ifndef S_MU2000_UI_PLAYER_H
#define S_MU2000_UI_PLAYER_H

#pragma once

#include "bridge.h"
#include "smf.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ui {

class player
{
public:
	// くり返し: しない（一覧の終わりで止まる）・一覧を回す・1 曲を回す・順番を混ぜて回す
	enum class loop_mode { none, all, one, shuffle };
	enum class state { stopped, playing, paused, chasing };
	struct entry {
		std::string path;
		std::string name;     // ファイル名
		std::string title;    // 曲名（ファイルに無ければ空）
		double length = 0;    // 秒
		int ports = 1;        // 使っている口の数
	};

	~player() { stop(); }

	// ---- 前からの口（メニューの「MIDI ファイルを再生」、窓へ落としたファイル、--play）
	// 一覧に足して（もうあればそれを）鳴らす。だめなら false（理由は err）
	bool start(const std::string &path, bridge &br, std::string &err);
	// 止めて、鳴りっぱなしを消す（一覧は残る）
	void stop();
	// 鳴らしているか（一時停止・追いかけ中も真）
	bool playing() const { return m_playing.load(std::memory_order_acquire); }

	// 音源が USB の口（A-D の 4 口）で受けるか。偽なら DIN の A・B だけ（--host-midi）
	void set_usb_ports(bool on) { m_usb.store(on, std::memory_order_relaxed); }
	// DIN の口だけのとき、3 口目以降（口 3・4）を A・B に重ねて鳴らすか（偽なら鳴らさない）。流している途中でも変えられる
	void set_fold_extra_ports(bool on) { m_fold.store(on, std::memory_order_relaxed); }
	bool fold_extra_ports() const { return m_fold.load(std::memory_order_relaxed); }
	// 重い MIDI を軽くするか（詰まったピッチベンドの間引きと、Roland の液晶のデータを送らない。
	// 既定は切り。bend_thinner.h、issue #82）。
	// 実機と同じ遅れを避けたいとき用で、実機の鳴り方からは外れる
	void set_thin_bends(bool on) { m_thin.store(on, std::memory_order_relaxed); }
	bool thin_bends() const { return m_thin.load(std::memory_order_relaxed); }

	// ---- 一覧
	std::vector<entry> list() const;
	int current() const { return m_cur.load(std::memory_order_relaxed); }   // 鳴らしている（か選んでいる）曲。-1 = なし
	// 足す（読めなければ false）。もう入っている道なら足さずに、その番号を返す
	bool add(const std::string &path, std::string &err, int *index = nullptr);
	void remove(int index);              // 鳴らしている曲を消したら止まる
	void move(int from, int to);         // 並べ替え
	void clear();                        // 止めて、全部消す

	// ---- 操作
	void play(int index, bridge &br);    // その曲を頭から
	void next();                         // 次の曲（くり返しの決まりに従う。無ければ止まる）
	void prev();                         // 2 秒より進んでいれば曲の頭へ、そうでなければ前の曲
	void pause(bool on);
	void seek(double sec);               // 曲の中の位置へ（鳴らしている・一時停止のときだけ）
	void set_loop(loop_mode m) { m_loop.store(int(m), std::memory_order_relaxed); }
	loop_mode loop() const { return loop_mode(m_loop.load(std::memory_order_relaxed)); }

	// ---- 表示
	state status() const { return state(m_state.load(std::memory_order_relaxed)); }
	// 開いたファイルが使っている口の数（1〜）
	int ports_used() const { return m_ports_used.load(std::memory_order_relaxed); }
	std::string name() const;            // 鳴らしている曲の曲名（無ければファイル名）
	double position() const { return m_pos.load(std::memory_order_relaxed); }
	double length() const   { return m_len.load(std::memory_order_relaxed); }
	// いまの小節・拍・テンポ（鳴らしている曲があるとき真）
	bool beat(int &bar, int &beat, double &bpm) const;

private:
	void run(bridge &br);
	void launch(bridge &br);
	int after(int index, int step);      // 次に鳴らす番号（無ければ -1）。m_lock を持って呼ぶ
	void reshuffle(int first);

	mutable std::mutex m_lock;           // m_list・m_order・m_meta
	std::vector<entry> m_list;
	std::vector<int>   m_order;          // 混ぜた順番（shuffle）
	smf::song_meta     m_meta;           // 鳴らしている曲の目盛り

	std::thread        m_thread;
	std::atomic<bool>  m_quit{false};
	std::atomic<bool>  m_playing{false};
	std::atomic<int>   m_state{0};
	std::atomic<int>   m_cur{-1};
	std::atomic<int>   m_jump{-1};       // この番号の曲へ（-1 = なし）
	std::atomic<int>   m_step{0};        // +1 = 次、-1 = 前
	std::atomic<bool>  m_paused{false};
	std::atomic<double> m_seek{-1.0};    // ここへ飛ぶ（負 = なし）
	std::atomic<double> m_pos{0}, m_len{0};
	std::atomic<int>   m_loop{0};
	std::atomic<int>   m_ports_used{1};
	std::atomic<bool>  m_fold{true};
	std::atomic<bool>  m_thin{false};
	std::atomic<bool>  m_usb{true};
};

} // namespace ui

#endif // S_MU2000_UI_PLAYER_H
