//	AltirraSDL - Profile Management dialog
//	Matches Windows Profiles dialog (uiprofiles.cpp)

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/vdstl.h>
#include <cstring>

#include "ui_main.h"
#include "simulator.h"
#include "settings.h"
#include "ui_profile_services.h"
#include <vd2/system/registry.h>
#include <string>
#include <algorithm>

extern void ATRegistryFlushToDisk();
#ifdef ALTIRRA_NETPLAY_ENABLED
#include "netplay/netplay_glue.h"
#endif

extern ATSimulator g_sim;

namespace {
	bool g_editOpen = false;
	uint32 g_editId = kATProfileId_Invalid;
	uint32 g_parentId = 0;
	ATSettingsCategory g_categoryMask = kATSettingsCategory_None;
	std::string g_editError;
	std::string g_profileNotice;

	VDStringA ProfileName(uint32 id) {
		return id ? VDTextWToU8(ATSettingsProfileGetName(id)) : VDStringA("Global profile");
	}

	void OpenProfileEditor(uint32 id) {
		g_editId = id;
		g_parentId = ATSettingsProfileGetParent(id);
		g_categoryMask = ATSettingsProfileGetCategoryMask(id);
		g_editError.clear();
		g_editOpen = true;
	}

	void RenderProfileEditor(ATSimulator& sim) {
		if (!g_editOpen)
			return;
		if (!ATSettingsIsValidProfile(g_editId)) {
			g_editOpen = false;
			return;
		}
		ATUIConstrainDialogSize();
		ImGui::SetNextWindowSize(ATUIFitDialogSize(ImVec2(620, 600)), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (!ImGui::Begin("Edit Profile", &g_editOpen,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return;
		}
		ATUIClampDialogPosition();
		if (ATUICheckEscClose()) {
			g_editOpen = false;
			ImGui::End();
			return;
		}
		const bool active = ATSettingsGetCurrentProfileId() == g_editId;
		float footerHeight = ImGui::GetFrameHeightWithSpacing()
			+ ImGui::GetStyle().ItemSpacing.y * 2;
		if (active) footerHeight += ImGui::GetTextLineHeightWithSpacing() * 2;
		if (!g_editError.empty())
			footerHeight += ImGui::CalcTextSize(g_editError.c_str(), nullptr, false,
				ImGui::GetContentRegionAvail().x).y + ImGui::GetStyle().ItemSpacing.y;
		ImGui::BeginChild("##definition", ImVec2(0, -footerHeight));
		ImGui::Text("Profile: %s", ProfileName(g_editId).c_str());
		ImGui::TextWrapped("Selected categories are saved in this profile. Other categories are inherited from its parent. Changes to inherited settings normally update the parent.");
		ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Parent profile").x
			- ImGui::GetStyle().ItemInnerSpacing.x);
		if (ImGui::BeginCombo("Parent profile", ProfileName(g_parentId).c_str())) {
			vdfastvector<uint32> ids;
			ATSettingsProfileEnum(ids);
			ids.insert(ids.begin(), 0);
			for (uint32 id : ids) {
				if (!ATUIProfileCanParent(g_editId, id))
					continue;
				ImGui::PushID((int)id);
				if (ImGui::Selectable(ProfileName(id).c_str(), id == g_parentId))
					g_parentId = id;
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SeparatorText("Save locally in this profile");
		bool all = g_categoryMask == kATSettingsCategory_All;
		if (ImGui::Checkbox("All categories (independent configuration)", &all))
			g_categoryMask = all ? kATSettingsCategory_All : kATSettingsCategory_None;
		ImGui::SameLine();
		if (ImGui::SmallButton("Inherit all"))
			g_categoryMask = kATSettingsCategory_None;
		size_t count;
		const auto *categories = ATUIProfileCategories(count);
		for (size_t i = 0; i < count; ++i) {
			bool owns = (g_categoryMask & categories[i].bit) != 0;
			if (ImGui::Checkbox(categories[i].name, &owns)) {
				uint32 mask = g_categoryMask & kATSettingsCategory_AllCategories;
				if (owns) mask |= categories[i].bit;
				else mask &= ~categories[i].bit;
				g_categoryMask = mask == kATSettingsCategory_AllCategories
					? kATSettingsCategory_All : (ATSettingsCategory)mask;
			}
			ImGui::SetItemTooltip("%s", categories[i].description);
		}
		ImGui::TextWrapped("Mounted images stores file references. Profiles do not embed ROMs, disk files or the running emulator state.");
		ImGui::EndChild();
		ImGui::Separator();
		if (!g_editError.empty())
			ImGui::TextWrapped("%s", g_editError.c_str());
		if (active)
			ImGui::TextWrapped("Saving reloads the active profile and resets the machine.");
		if (ImGui::Button("Save profile definition")) {
			try {
				if (!ATUIProfileCanParent(g_editId, g_parentId))
					throw MyError("Choose a parent that does not inherit from this profile.");
				if (active && ATSettingsGetTemporaryProfileMode())
					throw MyError("Finish the temporary session before changing the active profile definition.");
				if (active)
					ATSaveSettings(ATSettingsProfileGetCategoryMask(g_editId));
				ATSettingsProfileSetParent(g_editId, g_parentId);
				ATSettingsProfileSetCategoryMask(g_editId, g_categoryMask);
				if (active) {
					const bool running = sim.IsRunning();
					ATSaveSettings(g_categoryMask);
					ATSettingsLoadProfile(g_editId, kATSettingsCategory_All);
					sim.ColdReset();
					if (running) sim.Resume();
				}
				ATRegistryFlushToDisk();
				g_editOpen = false;
			} catch (const MyError& e) {
				g_editError = e.c_str();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			g_editOpen = false;
		ImGui::End();
	}
}

void ATUIRenderProfiles(ATSimulator &sim, ATUIState &state) {
#ifdef ALTIRRA_NETPLAY_ENABLED
	// Defensive: if the dialog was already open when an Online Play
	// session began (the menu gate only blocks new opens, not
	// pre-existing windows), close it.  Editing / switching profiles
	// while the canonical Online Play profile is active would break
	// the session.
	// IsSessionEngaged() — auto-close only once a peer is engaged.
	// Merely hosting (WaitingForJoiner) shouldn't block profile edits.
	if (ATNetplayGlue::IsSessionEngaged()) {
		state.showProfiles = false;
		return;
	}
#endif
	ATUIConstrainDialogSize();
	ImGui::SetNextWindowSize(ATUIFitDialogSize(ImVec2(650, 510)), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Profiles", &state.showProfiles, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	ATUIClampDialogPosition();
	static char renameBuffer[128] = {};
	static uint32 renamingId = kATProfileId_Invalid;
	static bool focusRename = false;
	if (ImGui::IsWindowAppearing()) {
		renamingId = kATProfileId_Invalid;
		focusRename = false;
		g_editOpen = false;
		g_editError.clear();
		g_profileNotice.clear();
	}
	if (ATUICheckEscClose()) {
		if (renamingId != kATProfileId_Invalid) {
			renamingId = kATProfileId_Invalid;
			focusRename = false;
		} else {
			state.showProfiles = false;
			ImGui::End();
			return;
		}
	}

	uint32 currentId = ATSettingsGetCurrentProfileId();

	// Enumerate profiles
	vdfastvector<uint32> profileIds;
	ATSettingsProfileEnum(profileIds);

	// Default profile names
	static const char *kDefaultProfileNames[] = {
		"400/800", "1200XL", "XL/XE", "XEGS", "5200"
	};

	// Show current profile
	VDStringW curName = ATSettingsProfileGetName(currentId);
	VDStringA curNameU8 = VDTextWToU8(curName);
	ImGui::Text("Current profile: %s", curNameU8.c_str());

	const uint32 ownership = ATSettingsProfileGetCategoryMask(currentId);
	if (ownership == kATSettingsCategory_All)
		ImGui::TextDisabled("Independent configuration: all categories saved locally.");
	else
		ImGui::TextWrapped("Inherits unselected categories from %s. Edit to see details.",
			ProfileName(ATSettingsProfileGetParent(currentId)).c_str());

	if (ATUIIsGameProfileSession()) {
		ImGui::TextWrapped("Game profile session: changes are temporary until you update the saved profile.");
		const bool ownsGameSettings = (ownership & kATSettingsCategory_AllCategories
			& ~(kATSettingsCategory_MountedImages | kATSettingsCategory_FullScreen)) != 0;
		ImGui::BeginDisabled(!ownsGameSettings);
		if (ImGui::Button("Update saved profile")) {
			try {
				ATUIUpdateGameProfile();
				g_editError.clear();
				g_profileNotice = "Profile saved. Assigned games will use these settings.";
			} catch (const MyError& e) { g_editError = e.c_str(); }
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Save locally owned categories for all games sharing this profile. Inherited settings, mounted media and fullscreen are unchanged.");
		if (!ownsGameSettings)
			ImGui::TextWrapped("This profile inherits its game settings. Create an independent copy to save a separate setup.");
		if (!g_profileNotice.empty())
			ImGui::TextWrapped("%s", g_profileNotice.c_str());
		if (!g_editError.empty())
			ImGui::TextWrapped("%s", g_editError.c_str());
	}

	ImGui::BeginDisabled(ATUIIsGameProfileSession());
	bool temporary = ATSettingsGetTemporaryProfileMode();
	if (ImGui::Checkbox("Temporary Profile (don't save changes)", &temporary))
		ATSettingsSetTemporaryProfileMode(temporary);
	ImGui::SetItemTooltip("When enabled, changes are not saved to the profile on exit. Assigned game profiles use Update saved profile instead.");
	ImGui::EndDisabled();

	ImGui::Separator();

	// Default profiles section
	ImGui::SeparatorText("Default Profiles");
	ImGui::TextWrapped("Default profiles are used as presets for different hardware types.");

	if (ImGui::BeginTable("##DefaultProfiles", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Hardware", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < kATDefaultProfileCount; ++i) {
			uint32 defId = ATGetDefaultProfileId((ATDefaultProfile)i);
			VDStringW defName = ATSettingsProfileGetName(defId);
			VDStringA defNameU8 = VDTextWToU8(defName);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(kDefaultProfileNames[i]);
			ImGui::TableNextColumn();
			ImGui::Text("%s", defNameU8.c_str());
			ImGui::TableNextColumn();

			ImGui::PushID(i);
			if (ImGui::SmallButton("Switch")) {
				ATUISwitchProfile(defId);
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// User profiles section
	ImGui::SeparatorText("All Profiles");

	const bool compactFooter = ImGui::GetContentRegionAvail().x < 560;
	float listHeight = ImGui::GetContentRegionAvail().y
		- ImGui::GetFrameHeightWithSpacing() * (compactFooter ? 2 : 1)
		- ImGui::GetStyle().ItemSpacing.y;
	listHeight = std::max(40.0f, listHeight);

	if (ImGui::BeginChild("##ProfileList", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
		// Global profile (ID 0) — cannot be renamed or deleted
		{
			VDStringW globalName = ATSettingsProfileGetName(0);
			VDStringA globalNameU8 = VDTextWToU8(globalName);
			bool isActive = (currentId == 0);

			ImGui::PushID(0);
			if (ImGui::Selectable(globalNameU8.c_str(), isActive)) {
				if (!isActive || ATUIIsGameProfileSession()) {
					ATUISwitchProfile(0);
				}
			}
			if (isActive) {
				ImGui::SameLine();
				ImGui::TextDisabled("(active)");
			}
			ImGui::PopID();
		}

		// Other profiles
		for (uint32 id : profileIds) {
			VDStringW name = ATSettingsProfileGetName(id);
			VDStringA nameU8 = VDTextWToU8(name);
			bool isActive = (currentId == id);
			bool visible = ATSettingsProfileGetVisible(id);

			ImGui::PushID((int)id);

			if (!visible)
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

			if (renamingId == id) {
				ImGui::SetNextItemWidth(-100);
				if (focusRename)
					ImGui::SetKeyboardFocusHere();
				const bool entered = ImGui::InputText("##rename", renameBuffer,
					sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue
						| ImGuiInputTextFlags_AutoSelectAll);
				if (focusRename) {
					ImGui::SetScrollHereY();
					focusRename = false;
				}
				ImGui::SameLine();
				const bool done = ImGui::SmallButton("Done");
				if (entered || done) {
					ATSettingsProfileSetName(id, VDTextU8ToW(VDStringA(renameBuffer)).c_str());
					renamingId = kATProfileId_Invalid;
				}
			} else {
				if (ImGui::Selectable(nameU8.c_str(), isActive, ImGuiSelectableFlags_AllowDoubleClick)) {
					if (ImGui::IsMouseDoubleClicked(0)) {
						renamingId = id;
						focusRename = true;
						strncpy(renameBuffer, nameU8.c_str(), sizeof(renameBuffer) - 1);
					} else if (!isActive || ATUIIsGameProfileSession()) {
						ATUISwitchProfile(id);
					}
				}
			}

			ImGui::SetItemTooltip("Parent: %s. %s",
				ProfileName(ATSettingsProfileGetParent(id)).c_str(),
				ATSettingsProfileGetCategoryMask(id) == kATSettingsCategory_All
					? "All categories saved locally" : "Some or all categories inherited");

			// Context menu
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Switch To")) {
					ATUISwitchProfile(id);
				}
				if (ImGui::MenuItem("Edit categories and inheritance..."))
					OpenProfileEditor(id);
				if (ImGui::MenuItem("Rename")) {
					renamingId = id;
					focusRename = true;
					strncpy(renameBuffer, nameU8.c_str(), sizeof(renameBuffer) - 1);
				}

				bool vis = ATSettingsProfileGetVisible(id);
				if (ImGui::MenuItem("Visible", nullptr, vis))
					ATSettingsProfileSetVisible(id, !vis);

				ImGui::Separator();
				const bool hasChildren = std::any_of(profileIds.begin(), profileIds.end(),
					[id](uint32 child) { return ATSettingsProfileGetParent(child) == id; });
				if (ImGui::MenuItem("Delete", nullptr, false, !isActive && !hasChildren)) {
					ATSettingsProfileDelete(id);
					ATRegistryFlushToDisk();
				}
				if (hasChildren && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Other profiles inherit from this profile. Change their parent before deleting it.");
				ImGui::EndPopup();
			}
			if (isActive) {
				ImGui::SameLine();
				ImGui::TextDisabled("(active)");
			}

			if (!visible)
				ImGui::PopStyleVar();

			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	// Bottom buttons
	if (ImGui::Button("Add Profile")) {
		uint32 newId = ATUICreateProfileCopy(true);
		// Start renaming immediately
		renamingId = newId;
		focusRename = true;
		strncpy(renameBuffer, "New Profile", sizeof(renameBuffer) - 1);
	}

	ImGui::SetItemTooltip("Create an independent copy of the current configuration, including CPU, memory, devices and input.");
	ImGui::SameLine();
	if (ImGui::Button("Add inherited profile")) {
		const uint32 id = ATUICreateProfileCopy(false);
		OpenProfileEditor(id);
	}
	if (!compactFooter)
		ImGui::SameLine();
	ImGui::BeginDisabled(!currentId);
	if (ImGui::Button("Edit active profile"))
		OpenProfileEditor(currentId);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 80.0f);
	if (ImGui::Button("OK", ImVec2(80.0f, 0)))
		state.showProfiles = false;

	ImGui::End();
	RenderProfileEditor(sim);
}
