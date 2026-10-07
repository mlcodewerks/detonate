// license:BSD-3-Clause
//
// MIDI プレイヤーの窓（イシュー #124）。曲の一覧と、再生・一時停止・前後の曲・くり返し・頭出し。
// 中身は ui::player（player.h）が持つ。ここは見せて、押されたら頼むだけ。
// gui だけの窓（プラグインは DAW が曲を流すので持たない）。ヘッダだけにしてあるのは、ほかの PC の窓
// （PC_SRCS。プラグインにも入る）と一緒にリンクしないため。
#ifndef S_MU2000_UI_PLAYER_VIEW_H
#define S_MU2000_UI_PLAYER_VIEW_H

#pragma once

#include "lang.h"
#include "player.h"
#include "texts.h"
#include "xg_ui.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ui {

class player_view : public imgui_view
{
public:
	explicit player_view(player &p) : m_play(p) {}

	const wchar_t *title() const override
	{
		return get_lang() == lang::ja ? L"S-MU2000 MIDI プレイヤー" : L"S-MU2000 MIDI Player";
	}
	int default_width() const override  { return 640; }
	int default_height() const override { return 520; }

	void draw(xg::model &, const xg_snapshot &, bridge &br) override
	{
		// ファイルの窓で選んだもの（何曲でも）
		std::vector<std::string> picked;
		if (xgui::take_midi_paths(picked))
			add_all(picked, br);

		const ImGuiViewport *vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImGui::Begin("midi_player", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleVar();
		const float fs = ImGui::GetFontSize();
		const std::vector<player::entry> list = m_play.list();
		const int cur = m_play.current();
		const player::state st = m_play.status();
		const bool active = m_play.playing();

		// ---- いまの曲
		if (active && cur >= 0 && cur < int(list.size())) {
			const player::entry &e = list[size_t(cur)];
			ImGui::TextUnformatted(e.title.empty() ? e.name.c_str() : e.title.c_str());
			if (!e.title.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", e.name.c_str());
			}
		} else {
			ImGui::TextDisabled("%s", UI_TEXT(ply_idle, "Nothing is playing"));
		}

		// ---- 操作
		if (ImGui::Button(UI_TEXT(ply_prev, "Prev"), ImVec2(fs * 3.5f, 0)))
			m_play.prev();
		ImGui::SameLine();
		if (st == player::state::paused) {
			if (ImGui::Button(UI_TEXT(ply_play, "Play"), ImVec2(fs * 4.5f, 0)))
				m_play.pause(false);
		} else if (active) {
			if (ImGui::Button(UI_TEXT(ply_pause, "Pause"), ImVec2(fs * 4.5f, 0)))
				m_play.pause(true);
		} else {
			ImGui::BeginDisabled(list.empty());
			if (ImGui::Button(UI_TEXT(ply_play, "Play"), ImVec2(fs * 4.5f, 0)))
				m_play.play(cur >= 0 && cur < int(list.size()) ? cur : 0, br);
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!active);
		if (ImGui::Button(UI_TEXT(ply_stop, "Stop"), ImVec2(fs * 3.5f, 0)))
			m_play.stop();
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button(UI_TEXT(ply_next, "Next"), ImVec2(fs * 3.5f, 0)))
			m_play.next();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(fs * 9);
		int mode = int(m_play.loop());
		const std::string modes = std::string(UI_TEXT(ply_loop_none, "No loop")) + '\0' + UI_TEXT(ply_loop_all, "Loop the list") + '\0' +
		                          UI_TEXT(ply_loop_one, "Loop one song") + '\0' + UI_TEXT(ply_loop_shuffle, "Shuffle") + '\0';
		if (ImGui::Combo("##loop", &mode, modes.c_str()))
			m_play.set_loop(player::loop_mode(mode));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", UI_TEXT(ply_loop_tip, "No loop: play the list once and stop. Loop the list: start again from the top. Loop one song: repeat the current song. Shuffle: play the list in a random order, shuffled again each time round."));

		// ---- 位置。離したときに飛ぶ（引いている間は飛ばない。飛ぶたびに設定を追いかけ直すので）
		const double len = m_play.length();
		const double pos = m_play.position();
		if (!m_dragging)
			m_seek = float(pos);
		char label[48];
		std::snprintf(label, sizeof(label), "%d:%02d / %d:%02d", int(m_seek) / 60, int(m_seek) % 60, int(len) / 60, int(len) % 60);
		ImGui::BeginDisabled(!active || len <= 0);
		ImGui::SetNextItemWidth(-1);
		ImGui::SliderFloat("##seek", &m_seek, 0.0f, float(len > 0 ? len : 1.0), label, ImGuiSliderFlags_AlwaysClamp);
		m_dragging = ImGui::IsItemActive();
		if (ImGui::IsItemDeactivatedAfterEdit())
			m_play.seek(double(m_seek));
		ImGui::EndDisabled();

		// ---- 小節・拍・テンポ
		int bar = 1, beat = 1;
		double bpm = 120;
		if (m_play.beat(bar, beat, bpm)) {
			ImGui::Text(UI_TEXT(ply_beat_fmt, "Bar %d  Beat %d   Tempo %.1f"), bar, beat, bpm);
			if (st == player::state::chasing) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", UI_TEXT(ply_chasing, "catching up the settings..."));
			} else if (st == player::state::paused) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", UI_TEXT(ply_paused, "paused"));
			}
		} else {
			ImGui::TextDisabled("%s", " ");
		}
		ImGui::Separator();

		// ---- 一覧。ダブルクリックで鳴らす。行を引いて並べ替える。× で消す
		const float foot = ImGui::GetFrameHeightWithSpacing() * 2.0f + (m_note.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing());
		int remove = -1, play_now = -1, move_from = -1, move_to = -1;
		if (ImGui::BeginTable("playlist", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
		                      ImVec2(0, -foot))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs * 2.0f);
			ImGui::TableSetupColumn(UI_TEXT(ply_col_song, "Song"));
			ImGui::TableSetupColumn(UI_TEXT(ply_col_len, "Length"), ImGuiTableColumnFlags_WidthFixed, fs * 3.5f);
			ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, fs * 1.6f);
			ImGui::TableHeadersRow();
			for (int i = 0; i < int(list.size()); i++) {
				const player::entry &e = list[size_t(i)];
				ImGui::PushID(i);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				char num[16];
				std::snprintf(num, sizeof(num), "%d", i + 1);
				const bool is_cur = i == cur;
				if (ImGui::Selectable(num, is_cur, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
				                                   ImGuiSelectableFlags_AllowDoubleClick) &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					play_now = i;
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
					ImGui::SetDragDropPayload("smu_song", &i, sizeof(i));
					ImGui::TextUnformatted(e.title.empty() ? e.name.c_str() : e.title.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("smu_song")) {
						move_from = *static_cast<const int *>(pl->Data);
						move_to = i;
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::TableNextColumn();
				if (is_cur && active)
					ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", e.title.empty() ? e.name.c_str() : e.title.c_str());
				else
					ImGui::TextUnformatted(e.title.empty() ? e.name.c_str() : e.title.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", e.path.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%d:%02d", int(e.length) / 60, int(e.length) % 60);
				ImGui::TableNextColumn();
				if (ImGui::SmallButton("x"))
					remove = i;
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (play_now >= 0)
			m_play.play(play_now, br);
		if (move_from >= 0 && move_to >= 0)
			m_play.move(move_from, move_to);
		if (remove >= 0)
			m_play.remove(remove);

		// ---- 足す・消す
		if (xgui::file_dialogs() && xgui::midi_dialog()) {
			if (ImGui::Button(UI_TEXT(ply_add, "Add files...")))
				xgui::ask_open_midi();
			ImGui::SameLine();
		}
		ImGui::BeginDisabled(list.empty());
		if (ImGui::Button(UI_TEXT(ply_clear, "Clear the list")))
			m_play.clear();
		ImGui::EndDisabled();
		// 道を打って足す（ファイルの窓が無い所でも使える）
		ImGui::SetNextItemWidth(-fs * 5);
		const bool enter = ImGui::InputTextWithHint("##path", UI_TEXT(ply_path, "Path of a MIDI file"), m_path, sizeof(m_path),
		                                            ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if ((ImGui::Button(UI_TEXT(ply_add_path, "Add")) || enter) && m_path[0]) {
			add_all({ std::string(m_path) }, br);
			if (m_note.empty())
				m_path[0] = 0;
		}
		if (!m_note.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", m_note.c_str());
		ImGui::End();
	}

private:
	// 足す。何も鳴らしていなければ、足した最初の曲から鳴らす
	void add_all(const std::vector<std::string> &paths, bridge &br)
	{
		m_note.clear();
		int first = -1;
		for (const std::string &p : paths) {
			std::string err;
			int index = -1;
			if (m_play.add(p, err, &index)) {
				if (first < 0)
					first = index;
			} else {
				m_note = err;
			}
		}
		if (first >= 0 && !m_play.playing())
			m_play.play(first, br);
	}

	player &m_play;
	float m_seek = 0;
	bool m_dragging = false;
	char m_path[1024] = {};
	std::string m_note;       // 足せなかった理由
};

} // namespace ui

#endif // S_MU2000_UI_PLAYER_VIEW_H
