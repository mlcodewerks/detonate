// license:BSD-3-Clause
//
// Shared gui.ini keys and flat file IO for the standalone front ends
// (gui.cpp, gui_mac.cpp).
//
// The state structs stay per front end (different shapes: globals versus
// the app class, keep-lists, --nomidi handling), but the keys and the file
// format live here once, so a setting added on one side cannot be missed
// or misspelled on the other. Needs only <cstdio>, <string> and <vector>.

#ifndef S_MU2000_UI_SETTINGS_H
#define S_MU2000_UI_SETTINGS_H

#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace ui {

// gui.ini keys, in A B C D order for the MIDI IN ports.
// 5 つ目は口 E（マルチパートのプラグインボードが受け持つ口。mu2000::board_midi_in）
inline constexpr int IN_PORTS = 5;
inline constexpr const char *SET_IN_KEYS[IN_PORTS] = { "midi_in", "midi_in_b", "midi_in_c", "midi_in_d", "midi_in_e" };
inline constexpr const char *SET_OUT = "midi_out";
inline constexpr const char *SET_OUT_B = "midi_out_b";
inline constexpr const char *SET_OUT_MU = "midi_out_mu";
inline constexpr const char *SET_AUDIO_OUT = "audio_out";
inline constexpr const char *SET_AUDIO_IN = "audio_in";
inline constexpr const char *SET_CARD = "smartmedia";
inline constexpr const char *SET_PORTS34 = "ports34";
inline constexpr const char *SET_THIN_BENDS = "thin_bends";   // 再生でピッチベンドを間引く（1 / 0）
inline constexpr const char *SET_OUTPUT = "output";
inline constexpr const char *SET_BOARD = "board";             // 架空のプラグインボード（fc。無ければ空）
inline constexpr const char *SET_BOARD_PART = "board_part";   // そのパート（1-64）
inline constexpr const char *SET_BOARD2 = "board2";           // 差込口 2・3（PLG-2・PLG-3）。書き方は board と同じ
inline constexpr const char *SET_BOARD2_PART = "board2_part";
inline constexpr const char *SET_BOARD3 = "board3";
inline constexpr const char *SET_BOARD3_PART = "board3_part";
inline constexpr const char *SET_BOARD_DLS = "board_dls";     // DLS のボードが読むファイル（道。UTF-8）
inline constexpr const char *SET_BOARD_FILE = "board_file";   // オリジナルのボードのファイル（道。src/ui/user_boards.h）
inline constexpr const char *SET_VOLUME = "volume";
inline constexpr const char *SET_EDIT_OUT = "edit_out";   // 音色の窓の送り先（空はパネルの設定）

// The whole file as key/value pairs, in file order.
using settings_map = std::vector<std::pair<std::string, std::string>>;

// Reads the file, one key=value per line. False when the file cannot be
// opened (first run); malformed lines are skipped.
inline bool read_settings_file(const std::string &path, settings_map &out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char line[512];
	while (std::fgets(line, sizeof(line), f)) {
		std::string t(line);
		while (!t.empty() && (t.back() == '\n' || t.back() == '\r'))
			t.pop_back();
		const size_t eq = t.find('=');
		if (eq == std::string::npos)
			continue;
		out.emplace_back(t.substr(0, eq), t.substr(eq + 1));
	}
	std::fclose(f);
	return true;
}

inline bool write_settings_file(const std::string &path, const settings_map &in)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	for (const auto &kv : in)
		std::fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
	std::fclose(f);
	return true;
}

// The value for key, or null. The last match wins, as with the old per-line
// loops that kept overwriting.
inline const std::string *find_setting(const settings_map &m, const char *key)
{
	for (auto it = m.rbegin(); it != m.rend(); ++it)
		if (it->first == key)
			return &it->second;
	return nullptr;
}

// Everything gui.ini remembers, both directions. The two front ends keep
// these in different shapes (globals vs members, partial vs full loads),
// so each fills or reads one of these and collect()/apply() below do the
// file mapping once. in[] has 4 entries, like SET_IN_KEYS (both front ends
// run 4 MIDI ports).
struct remembered {
	std::string in[IN_PORTS];
	std::string out, out_b, out_mu;
	std::string audio_out;
	std::string audio_in;
	std::string card;
	float volume = 1.0f;  // the panel's VOLUME knob
	bool fold34 = true;   // ports34=fold (MIDI file ports 3+4 onto A+B)
	bool thin_bends = false; // thin_bends=1 (the player thins dense pitch bends)
	bool analog = false;  // output=analog (DC removed)
	std::string edit_out; // edit_out= (the voice window's send-to port; empty = the panel's ports)
	int board = 0;        // board=fc / fc16 (the imaginary plug-in board, mu2000::VBOARD_FC / VBOARD_FC16; 0 = none)
	int board_part = 1;   // board_part= (1-64)
	int board_more[2] = { 0, 0 };          // board2= / board3= (slots PLG-2 and PLG-3)
	int board_more_part[2] = { 2, 3 };     // board2_part= / board3_part=
	std::string board_dls; // board_dls= (the DLS file of the DLS board)
	std::string board_file; // board_file= (the user's own board, board=user / user16)
};

// Struct to file rows, in file order
inline settings_map collect_settings(const remembered &r)
{
	settings_map kv;
	for (int p = 0; p < IN_PORTS; p++)
		kv.emplace_back(SET_IN_KEYS[p], r.in[p]);
	kv.emplace_back(SET_OUT, r.out);
	kv.emplace_back(SET_OUT_B, r.out_b);
	kv.emplace_back(SET_OUT_MU, r.out_mu);
	kv.emplace_back(SET_AUDIO_OUT, r.audio_out);
	kv.emplace_back(SET_AUDIO_IN, r.audio_in);
	kv.emplace_back(SET_CARD, r.card);
	char vol[32];
	std::snprintf(vol, sizeof(vol), "%.3f", r.volume);
	kv.emplace_back(SET_VOLUME, vol);
	kv.emplace_back(SET_PORTS34, r.fold34 ? "fold" : "drop");
	kv.emplace_back(SET_THIN_BENDS, r.thin_bends ? "1" : "0");
	kv.emplace_back(SET_OUTPUT, r.analog ? "analog" : "digital");
	kv.emplace_back(SET_EDIT_OUT, r.edit_out);
	static const char *const BOARD_KINDS[6] = { "", "fc", "fc16", "dls", "user", "user16" };
	kv.emplace_back(SET_BOARD, BOARD_KINDS[r.board >= 0 && r.board < 6 ? r.board : 0]);
	kv.emplace_back(SET_BOARD2, BOARD_KINDS[r.board_more[0] >= 0 && r.board_more[0] < 6 ? r.board_more[0] : 0]);
	kv.emplace_back(SET_BOARD2_PART, std::to_string(r.board_more_part[0]));
	kv.emplace_back(SET_BOARD3, BOARD_KINDS[r.board_more[1] >= 0 && r.board_more[1] < 6 ? r.board_more[1] : 0]);
	kv.emplace_back(SET_BOARD3_PART, std::to_string(r.board_more_part[1]));
	kv.emplace_back(SET_BOARD_DLS, r.board_dls);
	kv.emplace_back(SET_BOARD_FILE, r.board_file);
	kv.emplace_back(SET_BOARD_PART, std::to_string(r.board_part));
	return kv;
}

// File rows to struct. Missing keys leave the struct's defaults, so callers
// can start from what they already have.
inline void apply_settings(const settings_map &kv, remembered &r)
{
	for (int p = 0; p < IN_PORTS; p++)
		if (const std::string *v = find_setting(kv, SET_IN_KEYS[p]))
			r.in[p] = *v;
	if (const std::string *v = find_setting(kv, SET_OUT))     r.out     = *v;
	if (const std::string *v = find_setting(kv, SET_OUT_B))   r.out_b   = *v;
	if (const std::string *v = find_setting(kv, SET_OUT_MU))  r.out_mu  = *v;
	if (const std::string *v = find_setting(kv, SET_AUDIO_OUT)) r.audio_out = *v;
	if (const std::string *v = find_setting(kv, SET_AUDIO_IN))  r.audio_in  = *v;
	if (const std::string *v = find_setting(kv, SET_CARD))    r.card    = *v;
	if (const std::string *v = find_setting(kv, SET_PORTS34)) r.fold34  = *v != "drop";
	if (const std::string *v = find_setting(kv, SET_THIN_BENDS)) r.thin_bends = *v == "1";
	if (const std::string *v = find_setting(kv, SET_OUTPUT))  r.analog  = *v == "analog";
	if (const std::string *v = find_setting(kv, SET_EDIT_OUT)) r.edit_out = *v;
	const auto board_kind = [](const std::string &v) { return v == "fc" ? 1 : v == "fc16" ? 2 : v == "dls" ? 3 : v == "user" ? 4 : v == "user16" ? 5 : 0; };
	if (const std::string *v = find_setting(kv, SET_BOARD))   r.board = board_kind(*v);
	if (const std::string *v = find_setting(kv, SET_BOARD2))  r.board_more[0] = board_kind(*v);
	if (const std::string *v = find_setting(kv, SET_BOARD3))  r.board_more[1] = board_kind(*v);
	for (int i = 0; i < 2; i++)
		if (const std::string *v = find_setting(kv, i ? SET_BOARD3_PART : SET_BOARD2_PART)) {
			const int n = std::atoi(v->c_str());
			r.board_more_part[i] = n >= 1 && n <= 64 ? n : i + 2;
		}
	if (const std::string *v = find_setting(kv, SET_BOARD_DLS)) r.board_dls = *v;
	if (const std::string *v = find_setting(kv, SET_BOARD_FILE)) r.board_file = *v;
	if (const std::string *v = find_setting(kv, SET_BOARD_PART)) {
		const int n = std::atoi(v->c_str());
		r.board_part = n >= 1 && n <= 64 ? n : 1;
	}
	if (const std::string *v = find_setting(kv, SET_VOLUME)) {
		if (!v->empty())
			r.volume = std::clamp(float(std::atof(v->c_str())), 0.0f, 1.0f);
	}
}

// A device index looked up by name, or -1 when it is not there.
inline int find_device(const std::vector<std::string> &names, const std::string &want)
{
	if (want.empty())
		return -1;
	for (size_t i = 0; i < names.size(); i++)
		if (names[i] == want)
			return int(i);
	return -1;
}

} // namespace ui

#endif // S_MU2000_UI_SETTINGS_H
