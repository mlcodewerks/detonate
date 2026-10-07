// license:BSD-3-Clause
//
// オリジナルのボード（src/vboard_user.h）の置き場と、いま開いているボード。
// ボードは設定のフォルダーの下の boards に、名前ごとに 1 ファイル（<名前>.smuboard）で置く。変えるたびに書く。
// サンプリングの窓の「波形を作る」が中身を作り、マスターの窓が挿す。どちらもここを見る。
// 音源（mu2000）へは bridge::post で渡す

#ifndef S_MU2000_UI_USER_BOARDS_H
#define S_MU2000_UI_USER_BOARDS_H

#pragma once

#include "bridge.h"
#include "../compat/paths.h"
#include "../vboard_user.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ui::user_boards {

using board = smu2000::vboard::user_board;
using program = smu2000::vboard::user_program;

inline constexpr const char *EXT = ".smuboard";

struct state {
	std::mutex lock;
	std::shared_ptr<const board> now;
	std::string path;
	bool unsaved = false;
};
inline state &st()
{
	static state s;
	return s;
}

// 置き場（無ければ作る）。作れなければ空
inline std::string dir()
{
	const std::string base = smu2000::ensure_config_dir();
	if (base.empty())
		return {};
	const std::string d = smu2000::join(base, "boards");
	return smu2000::ensure_dir(d) ? d : std::string();
}

// ボードの名前から、ファイルの名前に使える字だけを残す
inline std::string file_for(const char *name)
{
	std::string stem;
	for (const char *p = name; *p; p++)
		stem += (*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '-' || *p == '_' ? *p : '_';
	while (!stem.empty() && stem.back() == '_')
		stem.pop_back();
	if (stem.empty())
		stem = "board";
	const std::string d = dir();
	return d.empty() ? std::string() : smu2000::join(d, stem + EXT);
}

// 置き場にあるボード（ファイルの名前から拡張子を取ったもの）
inline std::vector<std::string> list()
{
	std::vector<std::string> out;
	const size_t n = std::char_traits<char>::length(EXT);
	for (const smu2000::dir_entry &e : smu2000::list_dir(dir()))
		if (e.name.size() > n && e.name.compare(e.name.size() - n, n, EXT) == 0)
			out.push_back(e.name.substr(0, e.name.size() - n));
	std::sort(out.begin(), out.end());
	return out;
}

inline std::shared_ptr<const board> current()
{
	std::lock_guard<std::mutex> g(st().lock);
	return st().now;
}
inline std::string current_path()
{
	std::lock_guard<std::mutex> g(st().lock);
	return st().path;
}
// いま開いているボードのファイルの名前（拡張子なし）。無ければ空
inline std::string current_stem()
{
	const std::string p = current_path();
	const size_t slash = p.find_last_of("/\\"), n = std::char_traits<char>::length(EXT);
	const std::string name = p.substr(slash == std::string::npos ? 0 : slash + 1);
	return name.size() > n ? name.substr(0, name.size() - n) : std::string();
}

// 起動のとき: 設定に覚えてあったボードを読んでおく（音源へは呼んだ側が渡す）
inline std::shared_ptr<const board> adopt(const std::string &path, std::string &err)
{
	std::shared_ptr<const board> b = smu2000::vboard::load_user_board(path, err);
	if (b) {
		std::lock_guard<std::mutex> g(st().lock);
		st().now = b;
		st().path = path;
	}
	return b;
}

inline void to_engine(bridge &br, std::shared_ptr<const board> b, const std::string &path)
{
	br.post([b, path](mu2000 &mu) {
		mu.set_user_board(b, path);
		return std::string();
	});
}

// 変えたボードを音源へ渡す。ファイルへはまだ書かない（つまみを引いている間は書かず、save で 1 度に書く）
inline void commit(bridge &br, std::shared_ptr<const board> b)
{
	std::string path;
	{
		std::lock_guard<std::mutex> g(st().lock);
		st().now = b;
		st().unsaved = true;
		if (st().path.empty())
			st().path = file_for(b->name);
		path = st().path;
	}
	to_engine(br, std::move(b), path);
}

// まだ書いていない変更をファイルへ。名前を変えていたら、ファイルの名前も変える（古いほうは消す）
inline bool save(bridge &br)
{
	std::shared_ptr<const board> b;
	std::string old;
	{
		std::lock_guard<std::mutex> g(st().lock);
		if (!st().unsaved || !st().now)
			return true;
		st().unsaved = false;
		b = st().now;
		old = st().path;
	}
	const std::string path = file_for(b->name);
	if (path.empty() || !smu2000::vboard::save_user_board(path, *b))
		return false;
	if (path != old) {
		if (!old.empty())
			std::remove(old.c_str());
		{
			std::lock_guard<std::mutex> g(st().lock);
			st().path = path;
		}
		to_engine(br, b, path);
	}
	return true;
}
inline bool unsaved()
{
	std::lock_guard<std::mutex> g(st().lock);
	return st().unsaved;
}

// 置き場のボードを開く
inline bool open(bridge &br, const std::string &stem, std::string &err)
{
	save(br);
	const std::string d = dir();
	if (d.empty()) {
		err = "no settings folder";
		return false;
	}
	const std::string path = smu2000::join(d, stem + EXT);
	std::shared_ptr<const board> b = adopt(path, err);
	if (!b)
		return false;
	to_engine(br, std::move(b), path);
	return true;
}

// 新しい空のボード。同じ名前のファイルがあれば、番号を足す
inline void create(bridge &br)
{
	save(br);
	auto b = std::make_shared<board>();
	const std::vector<std::string> have = list();
	for (int n = 1; n < 100; n++) {
		std::snprintf(b->name, sizeof(b->name), n == 1 ? "MY BOARD" : "MY BOARD %d", n);
		const std::string p = file_for(b->name);
		if (p.empty() || !smu2000::is_file(p))
			break;
	}
	{
		std::lock_guard<std::mutex> g(st().lock);
		st().path.clear();
	}
	commit(br, std::move(b));
	save(br);
}

} // namespace ui::user_boards

#endif // S_MU2000_UI_USER_BOARDS_H
