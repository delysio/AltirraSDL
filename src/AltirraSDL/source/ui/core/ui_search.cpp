#include <stdafx.h>
#include "ui_search.h"
#include "ui_main.h"
#include "ui_mode.h"
#include "ui_fonts.h"
#include "accel_sdl3.h"
#include <imgui_internal.h>
#include <at/atui/uicommandmanager.h>
#include <vd2/Dita/accel.h>
#include <vd2/system/text.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

extern ATUICommandManager g_ATUICommandMgr;
extern ATUIState g_uiState;

namespace {
	struct Setting {
		int category;
		const char *label;
		const char *path;
		const char *widget;
		const char *keywords;
	};
	const Setting kSettings[] = {
#include <ui_search_settings.inl>
	};
	char g_query[192] {};
	bool g_focus = false;
	int g_selected = 0;
	std::string g_highlight;
	std::string g_highlightTitle;
	int g_highlightFrames = 0;
	bool g_scrollPending = false;

	std::string Lower(std::string value) {
		for (char& c : value)
			c = (char)std::tolower((unsigned char)c);
		return value;
	}

	bool Matches(const std::string& value, const std::string& query) {
		size_t start = 0;
		while (start < query.size()) {
			const size_t end = query.find(' ', start);
			const std::string word = query.substr(start, end - start);
			if (!word.empty() && value.find(word) == std::string::npos)
				return false;
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		return true;
	}

	std::string CommandLabel(const char *name) {
		static const std::pair<const char *, const char *> overrides[] = {
			{"UI.GlobalSearch", "Search actions and settings"},
			{"Edit.SaveFrame", "Save screenshot"},
			{"Edit.SaveFrameTrueAspect", "Save screenshot with corrected aspect"},
			{"Edit.CopyFrame", "Copy screenshot to clipboard"},
			{"System.Configure", "Configure System"},
			{"File.BootImage", "Boot image"},
			{"File.OpenImage", "Open image"},
		};
		for (const auto& entry : overrides)
			if (!strcmp(entry.first, name))
				return entry.second;
		std::string out;
		const char *start = strchr(name, '.');
		start = start ? start + 1 : name;
		for (const char *p = start; *p; ++p) {
			if (p > start && std::isupper((unsigned char)*p)
				&& (std::islower((unsigned char)p[-1])
					|| (p[1] && std::islower((unsigned char)p[1]))))
				out += ' ';
			out += *p == '.' ? ' ' : *p;
		}
		return out;
	}

	struct Result {
		std::string label;
		std::string detail;
		const Setting *setting = nullptr;
		const ATUICommand *command = nullptr;
		bool enabled = true;
	};
}

void ATUIOpenGlobalSearch() {
	g_uiState.showGlobalSearch = true;
	g_focus = true;
	g_selected = 0;
}

void ATUIRenderSearchMenuButton() {
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const float available = ImGui::GetWindowWidth()
		- ImGui::GetCursorPosX() - ImGui::GetStyle().WindowPadding.x;
	const float fullWidth = ImGui::CalcTextSize("Search actions and settings...").x
		+ ImGui::GetStyle().FramePadding.x * 2;
	const char *label = available >= fullWidth + gap
		? "Search actions and settings..." : "Search";
	const float width = ImGui::CalcTextSize(label).x
		+ ImGui::GetStyle().FramePadding.x * 2;
	if (available < width + gap)
		return; // The Help menu and shortcut remain available at small sizes.
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - width - gap);
	if (ImGui::Button(label))
		ATUIOpenGlobalSearch();
	ImGui::SetItemTooltip("Find a command or configuration setting. %s",
		ATUIGetShortcutStringForCommand("UI.GlobalSearch"));
}

void ATUIRenderGlobalSearch(ATUIState& state) {
	if (!state.showGlobalSearch)
		return;
	ATUIConstrainDialogSize();
	const float bodySize = ImGui::GetFontSize();
	const float fontBase = ImGui::GetStyle().FontSizeBase;
	ImGui::SetNextWindowSize(ATUIFitDialogSize(ImVec2(
		std::max(680.0f, bodySize * 34), std::max(440.0f, bodySize * 23))), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Search actions and settings", &state.showGlobalSearch,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}
	ATUIClampDialogPosition();
	if (ATUICheckEscClose()) {
		state.showGlobalSearch = false;
		ImGui::End();
		return;
	}
	ImGui::PushFont(ATUIGetFontHeading(), fontBase * 1.35f);
	ImGui::TextUnformatted("Find an action or setting");
	ImGui::PopFont();
	ImGui::Spacing();
	if (g_focus || ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere();
		g_focus = false;
	}
	ImGui::SetNextItemWidth(-1);
	const bool queryChanged = ImGui::InputTextWithHint("##search", "Try controller, memory, PAL or screenshot",
		g_query, sizeof g_query, ImGuiInputTextFlags_AutoSelectAll);
	if (queryChanged)
		g_selected = 0;
	std::string query = Lower(g_query);
	const auto first = query.find_first_not_of(" \t\r\n");
	query = first == std::string::npos ? ""
		: query.substr(first, query.find_last_not_of(" \t\r\n") - first + 1);
	std::vector<Result> results;
	if (!query.empty()) {
		for (const auto& setting : kSettings) {
			if (!Matches(Lower(std::string(setting.label) + " " + setting.path
				+ " " + setting.keywords), query))
				continue;
			const auto *configure = g_ATUICommandMgr.GetCommand("System.Configure");
			const bool enabled = !configure || !configure->mpTestFn || configure->mpTestFn();
			std::string detail = "Setting: " + std::string(setting.path);
			if (!enabled) detail += " - unavailable in the current emulator state";
			results.push_back({setting.label, detail, &setting, nullptr, enabled});
		}
		vdfastvector<VDAccelToCommandEntry> commands;
		g_ATUICommandMgr.ListCommands(commands);
		for (const auto& entry : commands) {
			// These are paired key-down/key-up handlers. Invoking the press
			// alone would latch turbo; search exposes ToggleWarpSpeed instead.
			if (!strcmp(entry.mpName, "System.PulseWarpOn")
				|| !strcmp(entry.mpName, "System.PulseWarpOff"))
				continue;
			const auto *command = g_ATUICommandMgr.GetCommand(entry.mpName);
			const std::string label = CommandLabel(entry.mpName);
			if (!Matches(Lower(label + " " + entry.mpName), query))
				continue;
			const bool enabled = !command->mpTestFn || command->mpTestFn();
			const char *dot = strchr(entry.mpName, '.');
			std::string group(entry.mpName, dot ? dot - entry.mpName : strlen(entry.mpName));
			if (group == "Debug" || group == "Pane") group = "Debugger";
			if (group == "Record") group = "Recording";
			if (group == "UI") group = "Interface";
			std::string detail = "Action: " + group;
			const char *shortcut = ATUIGetShortcutStringForCommand(entry.mpName);
			if (shortcut && *shortcut)
				detail += "  (" + std::string(shortcut) + ")";
			if (!enabled)
				detail += " - unavailable in the current emulator state";
			else if (command->mpStateFn && command->mpStateFn() != kATUICmdState_None)
				detail += " - currently selected/enabled";
			results.push_back({label, detail, nullptr, command, enabled});
		}
		std::stable_sort(results.begin(), results.end(), [&](const Result& a, const Result& b) {
			const bool exactA = Lower(a.label) == query;
			const bool exactB = Lower(b.label) == query;
			if (exactA != exactB) return exactA;
			return a.label < b.label;
		});
	}
	if (!results.empty())
		g_selected = std::clamp(g_selected, 0, (int)results.size() - 1);
	bool keyboardSelection = false;
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			g_selected = std::min(g_selected + 1, (int)results.size() - 1);
			keyboardSelection = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			g_selected = std::max(0, g_selected - 1);
			keyboardSelection = true;
		}
	}
	const bool activate = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& ImGui::IsKeyPressed(ImGuiKey_Enter);
	ImGui::Separator();
	const Result *chosen = nullptr;
	char footer[160];
	snprintf(footer, sizeof footer, "%zu results  |  Up/Down to select, Enter to open, Escape to close", results.size());
	ImGui::PushFont(ATUIGetFontUI(), fontBase * 0.9f);
	const float footerHeight = ImGui::CalcTextSize(footer, nullptr, false,
		ImGui::GetContentRegionAvail().x).y + ImGui::GetStyle().ItemSpacing.y * 2;
	ImGui::PopFont();
	ImGui::BeginChild("##results", ImVec2(0, -footerHeight),
		ImGuiChildFlags_AlwaysUseWindowPadding);
	if (queryChanged || ImGui::IsWindowAppearing())
		ImGui::SetScrollY(0);
	if (query.empty())
		ImGui::TextWrapped("Find actions and settings without searching through menus. Type to begin.");
	else if (results.empty())
		ImGui::TextWrapped("No matches. Try a shorter term or a related word.");
	for (size_t i = 0; i < results.size(); ++i) {
		const Result& result = results[i];
		ImGui::PushID((int)i);
		ImGui::BeginDisabled(!result.enabled);
		// Selectable expands its hit rectangle into ItemSpacing. Text must
		// start at the content cursor, rather than that expanded rectangle.
		const ImVec2 row = ImGui::GetCursorScreenPos();
		const float rowGap = ImGui::GetStyle().ItemSpacing.y;
		ImGui::PushFont(ATUIGetFontHeading(), fontBase * 1.08f);
		const float titleHeight = ImGui::GetTextLineHeight();
		if (ImGui::Selectable(result.label.c_str(), g_selected == (int)i,
			0, ImVec2(0, titleHeight + bodySize * 0.9f + rowGap * 2)))
			chosen = &result;
		ImGui::PopFont();
		if (keyboardSelection && g_selected == (int)i)
			ImGui::SetScrollHereY();
		ImGui::GetWindowDrawList()->AddText(
			ATUIGetFontUI(), bodySize * 0.9f,
			ImVec2(row.x, row.y + titleHeight + rowGap),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), result.detail.c_str());
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s\n%s", result.label.c_str(), result.detail.c_str());
		ImGui::EndDisabled();
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::PushFont(ATUIGetFontUI(), fontBase * 0.9f);
	ImGui::PushTextWrapPos(0);
	ImGui::TextDisabled("%s", footer);
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	ImGui::End();
	if (activate && g_selected >= 0 && g_selected < (int)results.size()
		&& results[g_selected].enabled)
		chosen = &results[g_selected];
	if (!chosen)
		return;
	state.showGlobalSearch = false;
	if (chosen->setting) {
		state.systemConfigCategory = chosen->setting->category;
		state.showSystemConfig = true;
		g_highlight = chosen->setting->widget;
		g_highlightTitle = chosen->setting->label;
		g_highlightFrames = 180;
		g_scrollPending = true;
	} else {
		g_ATUICommandMgr.ExecuteCommandNT(*chosen->command);
	}
}

bool ATUIBeginSearchSettingsPage() {
	const bool previous = GImGui->TestEngineHookItems;
	if (g_highlightFrames > 0) {
		--g_highlightFrames;
		ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_NavCursor),
			"Search result: %s", g_highlightTitle.c_str());
		ImGui::Separator();
		GImGui->TestEngineHookItems = true;
	}
	return previous;
}

void ATUIEndSearchSettingsPage(bool previousHooks) {
	GImGui->TestEngineHookItems = previousHooks;
}

const char *ATUISearchSettingLabel(ImGuiContext *ctx, ImGuiID id) {
	if (!ctx->CurrentWindow || !id)
		return nullptr;
	if (strstr(ctx->CurrentWindow->Name, "##SysCfgContent")) {
		for (const auto& setting : kSettings)
			if (*setting.widget && ctx->CurrentWindow->GetID(setting.widget) == id)
				return setting.widget;
	}
	for (const char *label : {"Parent profile", "Launch profile"})
		if (ctx->CurrentWindow->GetID(label) == id)
			return label;
	return nullptr;
}

void ATUISearchSettingAdd(ImGuiContext *ctx, ImGuiID id,
	const ImVec2& min, const ImVec2& max) {
	if (g_highlightFrames <= 0 || g_highlight.empty() || !ctx->CurrentWindow
		|| !strstr(ctx->CurrentWindow->Name, "##SysCfgContent")
		|| ctx->CurrentWindow->GetID(g_highlight.c_str()) != id)
		return;
	if (g_scrollPending) {
		ImGui::SetScrollFromPosY(ctx->CurrentWindow,
			min.y - ctx->CurrentWindow->Pos.y, 0.35f);
		g_scrollPending = false;
	}
	ctx->CurrentWindow->DrawList->AddRect(ImVec2(min.x - 2, min.y - 2),
		ImVec2(max.x + 2, max.y + 2), ImGui::GetColorU32(ImGuiCol_NavCursor));
}
