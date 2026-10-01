#include <stdafx.h>
#include "ui_guided_input.h"
#include "input_capture.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "input_selection.h"
#include "inputdefs.h"
#include "input_sdl3.h"
#include "joystick_sdl3.h"
#include "simulator.h"
#include "settings.h"
#include <imgui.h>
#include <vd2/system/text.h>
#include <array>
#include <vector>
#include <algorithm>

namespace {
const char *const kActions[] = { "Up", "Down", "Left", "Right", "Fire" };
const uint32 kTargets[] = {
	kATInputTrigger_Up, kATInputTrigger_Down, kATInputTrigger_Left,
	kATInputTrigger_Right, kATInputTrigger_Button0
};
const wchar_t *const kMapNames[] = {
	L"Guided setup joystick (Port 1)", L"Guided setup joystick (Port 2)"
};
struct Draft {
	vdrefptr<ATInputMap> original;
	std::array<std::vector<uint32>, 5> bindings;
	std::array<bool, 5> changed {};
	int source = 0; // keyboard, controller, keyboard + controller
	int unit = -1; // detect on first deliberate controller input
};
struct ActiveMap {
	vdrefptr<ATInputMap> map;
	bool keep = true;
};
bool s_open = false;
bool s_appearing = false;
bool s_captureOwned = false;
int s_port = 0;
int s_captureAction = -1;
int s_captureBinding = 0; // -1 = append
bool s_sequence = false;
bool s_review = false;
uint32 s_profile = 0;
ImVec2 s_viewportSize;
std::array<Draft, 2> s_drafts;
std::vector<ActiveMap> s_activeMaps;
ATJoystickManagerSDL3 *s_manager = nullptr;
const char *s_notice = "";

bool IsStandardBinding(const ATInputMap& map,
	const ATInputMap::Mapping& binding, int port, int action)
{
	const auto& controller = map.GetController(binding.mControllerId);
	// Only edit unconditional default-mode bindings. Preserve autofire,
	// modifiers, conditional inputs and other controllers in the same map.
	return controller.mType == kATInputControllerType_Joystick
		&& controller.mIndex == (uint32)port
		&& binding.mCode == kTargets[action]
		&& !(binding.mInputCode & (kATInputCode_FlagMask
			| kATInputCode_SpecificUnit));
}

void LoadDraft(ATInputManager& im, int port) {
	auto& draft = s_drafts[port];
	draft = {};
	for (uint32 i = 0; i < im.GetInputMapCount(); ++i) {
		vdrefptr<ATInputMap> map;
		if (im.GetInputMapByIndex(i, ~map)
			&& !wcscmp(map->GetName(), kMapNames[port])) {
			draft.original = map;
			break;
		}
	}
	if (!draft.original)
		return;
	draft.unit = draft.original->GetSpecificInputUnit();
	bool keyboard = false;
	bool controller = false;
	for (uint32 i = 0; i < draft.original->GetMappingCount(); ++i) {
		const auto& binding = draft.original->GetMapping(i);
		for (int action = 0; action < 5; ++action) {
			if (!IsStandardBinding(*draft.original, binding, port, action))
				continue;
			draft.bindings[action].push_back(binding.mInputCode);
			if ((binding.mInputCode & kATInputCode_ClassMask) == kATInputCode_JoyClass)
				controller = true;
			else
				keyboard = true;
		}
	}
	draft.source = keyboard && controller ? 2 : controller ? 1 : 0;
}

void StopCapture() {
	ATInputCapture::Stop();
	s_captureAction = -1;
	s_sequence = false;
}

void Close() {
	StopCapture();
	if (s_captureOwned && s_manager)
		s_manager->SetBindingCapture(false);
	s_captureOwned = false;
	s_manager = nullptr;
	s_open = false;
	s_review = false;
	s_drafts = {};
	s_activeMaps.clear();
}

void StartCapture(int action, int binding, bool sequence) {
	auto& draft = s_drafts[s_port];
	s_captureAction = action;
	s_captureBinding = binding;
	s_sequence = sequence;
	s_notice = "";
	ATInputCapture::Start(draft.source != 1, draft.source != 0, draft.unit);
}

VDStringA InputName(ATInputManager& im, uint32 code, int unit) {
	VDStringW name;
	if (!s_manager || !s_manager->GetBindingName(unit, code, name))
		im.GetNameForInputCode(code, name);
	return VDTextWToU8(name);
}

bool HasControllerBindings(const Draft& draft) {
	for (const auto& bindings : draft.bindings) {
		for (uint32 code : bindings) {
			if ((code & kATInputCode_ClassMask) == kATInputCode_JoyClass)
				return true;
		}
	}
	return false;
}

bool Complete(const Draft& draft) {
	return std::all_of(draft.bindings.begin(), draft.bindings.end(),
		[](const auto& bindings) { return !bindings.empty(); });
}

void BeginReview(ATInputManager& im) {
	s_activeMaps.clear();
	for (uint32 i = 0; i < im.GetInputMapCount(); ++i) {
		vdrefptr<ATInputMap> map;
		if (!im.GetInputMapByIndex(i, ~map)
			|| !im.IsInputMapEnabled(map)
			|| map.get() == s_drafts[s_port].original.get())
			continue;
		// Include both ports: an Any-gamepad map on port 1 can make the
		// physical controller being configured for port 2 drive BOTH ports.
		if (map->UsesPhysicalPort(0) || map->UsesPhysicalPort(1))
			s_activeMaps.push_back({ map, true });
	}
	s_review = true;
}

void Apply(ATInputManager& im) {
	auto& draft = s_drafts[s_port];
	vdrefptr<ATInputMap> map(new ATInputMap);
	uint32 controllerId = 0;
	bool foundController = false;
	if (draft.original) {
		for (uint32 c = 0; c < draft.original->GetControllerCount(); ++c) {
			const auto& controller = draft.original->GetController(c);
			map->AddController(controller.mType, controller.mIndex);
			if (controller.mType == kATInputControllerType_Joystick
				&& controller.mIndex == (uint32)s_port) {
				controllerId = c;
				foundController = true;
			}
		}
		for (uint32 i = 0; i < draft.original->GetMappingCount(); ++i) {
			const auto& binding = draft.original->GetMapping(i);
			bool replaced = false;
			for (int action = 0; action < 5; ++action) {
				if (draft.changed[action] && IsStandardBinding(
					*draft.original, binding, s_port, action))
					replaced = true;
			}
			if (!replaced)
				map->AddMapping(binding.mInputCode, binding.mControllerId, binding.mCode);
		}
		map->SetQuickMap(draft.original->IsQuickMap());
	}
	if (!foundController)
		controllerId = map->AddController(kATInputControllerType_Joystick, s_port);
	for (int action = 0; action < 5; ++action) {
		if (draft.original && !draft.changed[action])
			continue;
		for (uint32 code : draft.bindings[action])
			map->AddMapping(code, controllerId, kTargets[action]);
	}
	map->SetSpecificInputUnit(draft.unit);
	for (const auto& active : s_activeMaps) {
		if (!active.keep)
			im.ActivateInputMap(active.map, false);
	}
	if (draft.original)
		im.RemoveInputMap(draft.original);
	map->SetName(kMapNames[s_port]);
	im.AddInputMap(map);
	im.ActivateInputMap(map, true);
	ATInputSelection::CommitMapsAndSelections();
	LoadDraft(im, s_port);
	s_review = false;
	s_notice = "Joystick setup saved. Its map is available in Input Mappings.";
}
}

void ATUIOpenGuidedJoystickSetup(int port) {
	if (s_open)
		return;
	s_port = port == 1 ? 1 : 0;
	s_open = true;
	s_appearing = true;
	s_viewportSize = ImVec2(0, 0);
}

bool ATUIIsGuidedJoystickSetupOpen() { return s_open; }
void ATUIShutdownGuidedJoystickSetup() { Close(); }

void ATUIRenderGuidedJoystickSetup(ATSimulator& sim) {
	if (!s_open)
		return;
	ATInputManager *im = sim.GetInputManager();
	if (!im) {
		Close();
		return;
	}
	if (s_appearing) {
		s_profile = ATSettingsGetCurrentProfileId();
		LoadDraft(*im, 0);
		LoadDraft(*im, 1);
		s_manager = static_cast<ATJoystickManagerSDL3 *>(sim.GetJoystickManager());
		if (s_manager) {
			s_manager->SetBindingCapture(true);
			s_captureOwned = true;
		}
		ATInputSDL3_ReleaseAllKeys();
		s_notice = "";
		ImGui::OpenPopup("Guided Joystick Setup");
		s_appearing = false;
	}
	ImGui::SetNextWindowSize(ImVec2(650, 0), ImGuiCond_Appearing);
	const ImVec2 available = ImGui::GetMainViewport()->WorkSize;
	ImGui::SetNextWindowSizeConstraints(
		ImVec2(std::min(650.0f, available.x - 24.0f), 0),
		ImVec2(available.x - 24.0f, available.y - 24.0f));
	// Recenter after resize/rotation as well as on open. Preserve manual
	// positioning between viewport changes without storing it in imgui.ini.
	const bool resized = available.x != s_viewportSize.x
		|| available.y != s_viewportSize.y;
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		resized ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	s_viewportSize = available;
	bool open = true;
	if (!ImGui::BeginPopupModal("Guided Joystick Setup", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
		Close();
		return;
	}
	if (!open || sim.GetHardwareMode() == kATHardwareMode_5200
		|| s_profile != ATSettingsGetCurrentProfileId()) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		Close();
		return;
	}
	const bool capturing = s_captureAction >= 0;
	if (!capturing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		Close();
		return;
	}
	ImGui::BeginDisabled(capturing || s_review);
	const char *ports[] = { "Joystick 1 - Port 1", "Joystick 2 - Port 2" };
	for (int port = 0; port < 2; ++port) {
		if (port && available.x >= 500.0f) ImGui::SameLine();
		if (ImGui::RadioButton(ports[port], &s_port, port))
			s_notice = "";
	}
	ImGui::EndDisabled();
	auto& draft = s_drafts[s_port];
	VDStringA mapName = VDTextWToU8(kMapNames[s_port], -1);
	ImGui::Text("%s", mapName.c_str());
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Guided setup finds its map by this name. Renaming it in Input Mappings keeps it as a separate map.");
	ImGui::TextWrapped(draft.original
		? "Apply updates and enables this existing map. It does not create another copy."
		: "Apply creates and enables one new input map with this name. Future guided setups reuse it.");
	ImGui::TextWrapped("Built-in presets and other maps are not replaced. "
		"You can edit this map in Input Mappings; advanced bindings are retained.");

	ImGui::BeginDisabled(capturing || s_review);
	const char *sources[] = { "Keyboard", "Controller", "Keyboard and controller" };
	ImGui::TextUnformatted("Capture from:");
	for (int source = 0; source < 3; ++source) {
		if (source && available.x >= 600.0f) ImGui::SameLine();
		ImGui::RadioButton(sources[source], &draft.source, source);
	}
	if (draft.source != 0 || HasControllerBindings(draft)) {
		const wchar_t *name = draft.unit >= 0 ? im->GetInputUnitName(draft.unit) : nullptr;
		VDStringA device = name ? VDTextWToU8(name, -1)
			: draft.unit < 0 ? VDStringA("Detect on first controller input")
			: VDStringA("Controller disconnected");
		if (ImGui::BeginCombo("Controller", device.c_str())) {
			if (ImGui::Selectable("Detect on next controller input", draft.unit < 0)) {
				draft.unit = -1;
			}
			for (int unit = 0; unit < 32; ++unit) {
				const wchar_t *unitName = im->GetInputUnitName(unit);
				if (!unitName)
					continue;
				VDStringA label = VDTextWToU8(unitName, -1);
				ImGui::PushID(unit);
				if (ImGui::Selectable(label.c_str(), draft.unit == unit)) {
					draft.unit = unit;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::TextWrapped("Controller selection applies to all controller bindings in this map.");
	}
	ImGui::Separator();
	if (!s_review) {
		if (ImGui::BeginTable("GuidedBindings", 3,
			ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 55);
			ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
				available.x >= 600.0f ? 220.0f : 155.0f);
			ImGui::TableHeadersRow();
			for (int action = 0; action < 5; ++action) {
				auto& bindings = draft.bindings[action];
				const int count = std::max(1, (int)bindings.size());
				ImGui::PushID(action);
				for (int binding = 0; binding < count; ++binding) {
					ImGui::PushID(binding);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (!binding)
						ImGui::TextUnformatted(kActions[action]);
					ImGui::TableNextColumn();
					VDStringA name = bindings.empty() ? VDStringA("Unassigned")
						: InputName(*im, bindings[binding], draft.unit);
					ImGui::TextWrapped("%s", name.c_str());
					ImGui::TableNextColumn();
					if (ImGui::Button("Wait for input"))
						StartCapture(action, binding, false);
					if (available.x >= 600.0f) ImGui::SameLine();
					if (!bindings.empty() && ImGui::SmallButton("Remove")) {
						bindings.erase(bindings.begin() + binding);
						draft.changed[action] = true;
						ImGui::PopID();
						break;
					}
					ImGui::PopID();
				}
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(2);
				if (ImGui::SmallButton("Add binding"))
					StartCapture(action, -1, false);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (ImGui::Button("Configure all"))
			StartCapture(0, 0, true);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Set the first binding for each action. Additional bindings are retained.");
		ImGui::SameLine();
		ImGui::BeginDisabled(!Complete(draft) || (HasControllerBindings(draft)
			&& draft.unit < 0));
		if (ImGui::Button("Review and apply..."))
			BeginReview(*im);
		ImGui::EndDisabled();
	}
	ImGui::EndDisabled();

	if (s_captureAction >= 0) {
		ATInputCapture::Result result;
		if (ATInputCapture::Poll(s_manager, result)) {
			auto& bindings = draft.bindings[s_captureAction];
			if (s_captureBinding < 0 || bindings.empty()) {
				if (std::find(bindings.begin(), bindings.end(), result.code) == bindings.end())
					bindings.push_back(result.code);
			} else {
				bindings[s_captureBinding] = result.code;
			}
			if (result.unit >= 0)
				draft.unit = result.unit;
			draft.changed[s_captureAction] = true;
			if (s_sequence && s_captureAction < 4)
				StartCapture(s_captureAction + 1, 0, true);
			else
				StopCapture();
		} else if (ATInputCapture::WasCancelled()) {
			StopCapture();
			s_notice = "Capture cancelled. Completed bindings are retained for review.";
		}
		if (s_captureAction >= 0) {
			ImGui::Separator();
			ImGui::Text("Waiting for input: %s", kActions[s_captureAction]);
			ImGui::TextWrapped("%s", ATInputCapture::GetPrompt());
			if (s_sequence && s_captureAction > 0) {
				if (ImGui::Button("Back"))
					StartCapture(s_captureAction - 1, 0, true);
				ImGui::SameLine();
			}
			if (ImGui::Button("Cancel capture"))
				StopCapture();
		}
	}
	if (s_review) {
		ImGui::TextWrapped("%s and enable: %s",
			draft.original ? "Update the existing map" : "Create a new map",
			mapName.c_str());
		ImGui::TextUnformatted("Bindings to apply:");
		for (int action = 0; action < 5; ++action) {
			VDStringA names;
			for (uint32 code : draft.bindings[action]) {
				if (!names.empty()) names += ", ";
				names += InputName(*im, code, draft.unit);
			}
			ImGui::TextWrapped("%s: %s", kActions[action], names.c_str());
		}
		ImGui::Separator();
		ImGui::TextWrapped("Other enabled maps on ports 1 or 2 are kept by default. "
			"Uncheck a map to disable it when you apply; it will not be deleted. "
			"An Any-controller map may make a controller operate both ports. "
			"Disabling a map also disables its controls on other ports.");
		ImGui::BeginChild("Active maps", ImVec2(0, 120), ImGuiChildFlags_Borders);
		for (auto& active : s_activeMaps) {
			VDStringA name = VDTextWToU8(active.map->GetName(), -1);
			ImGui::PushID(active.map.get());
			ImGui::Checkbox(name.c_str(), &active.keep);
			ImGui::PopID();
		}
		if (s_activeMaps.empty())
			ImGui::TextUnformatted("No other maps are enabled on ports 1 or 2.");
		ImGui::EndChild();
		if (ImGui::Button("Apply"))
			Apply(*im);
		ImGui::SameLine();
		if (ImGui::Button("Back to bindings"))
			s_review = false;
	}
	if (*s_notice)
		ImGui::TextWrapped("%s", s_notice);
	ImGui::Separator();
	ImGui::TextWrapped("Apply saves this port's map and your enabled-map choices. "
		"Closing discards unapplied setup changes on both ports.");
	if (ImGui::Button("Close")) {
		ImGui::CloseCurrentPopup();
		open = false;
	}
	ImGui::EndPopup();
	if (!open)
		Close();
}
