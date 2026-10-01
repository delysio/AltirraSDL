//	AltirraSDL - Light Pen/Gun configuration dialog
//	ImGui implementation matching Windows IDD_LIGHTPEN.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vectors.h>
#include "ui_main.h"
#include "simulator.h"
#include "inputcontroller.h"

extern ATSimulator g_sim;

void ATUIRenderLightPenDialog(ATSimulator &sim, ATUIState &state) {
	if (!state.showLightPen)
		return;

	ATLightPenPort *lpp = sim.GetLightPenPort();
	if (!lpp) {
		state.showLightPen = false;
		return;
	}

	ImGui::SetNextWindowSize(ATUIFitDialogSize(ImVec2(480, 250)), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Light Pen/Gun", &state.showLightPen,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showLightPen = false;
		ImGui::End();
		return;
	}

	// Match IDD_LIGHTPEN's horizontal/vertical grid and commit on OK,
	// as uilightpen.cpp does in OnDataExchange(true).
	static vdint2 gunAdjust;
	static vdint2 penAdjust;
	static int noiseMode = 0;
	if (ImGui::IsWindowAppearing()) {
		gunAdjust = lpp->GetAdjust(false);
		penAdjust = lpp->GetAdjust(true);
		noiseMode = (int)lpp->GetNoiseMode();
	}

	const float footerHeight = ImGui::GetFrameHeightWithSpacing()
		+ ImGui::GetStyle().ItemSpacing.y * 2.0f;
	ImGui::BeginChild("##LightPenBody", ImVec2(0, -footerHeight));
	ImGui::TextWrapped("Different hardware and games vary in light pen/gun "
		"positioning. Tweak adjustment values so that the emulation cursor "
		"matches target indicators in the game. You can also use the "
		"Recalibrate option to do this interactively.");
	ImGui::Spacing();
	if (ImGui::BeginTable("##Offsets", 3, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("##Device", ImGuiTableColumnFlags_WidthFixed,
			ImGui::CalcTextSize("Light gun").x + 16.0f);
		ImGui::TableSetupColumn("Horizontal");
		ImGui::TableSetupColumn("Vertical");
		ImGui::TableHeadersRow();
		vdint2 *adjustments[] = { &gunAdjust, &penAdjust };
		const char *labels[] = { "Light gun", "Light pen" };
		for (int row = 0; row < 2; ++row) {
			ImGui::PushID(row);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(labels[row]);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputInt("##Horizontal", &adjustments[row]->x, 1, 5))
				adjustments[row]->x = std::clamp(adjustments[row]->x, -64, 64);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputInt("##Vertical", &adjustments[row]->y, 1, 5))
				adjustments[row]->y = std::clamp(adjustments[row]->y, -64, 64);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::Spacing();
	static const char *kNoiseModes[] = {
		"None", "Low (CX-75 + 800)", "High (CX-75 + XL/XE)"
	};
	if (noiseMode < 0 || noiseMode >= 3) noiseMode = 0;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Noise mode");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::Combo("##NoiseMode", &noiseMode, kNoiseModes, 3);
	ImGui::EndChild();
	ImGui::Separator();
	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x
		- 160.0f - ImGui::GetStyle().ItemSpacing.x);
	if (ImGui::Button("OK", ImVec2(80.0f, 0))) {
		lpp->SetAdjust(false, gunAdjust);
		lpp->SetAdjust(true, penAdjust);
		lpp->SetNoiseMode((ATLightPenNoiseMode)noiseMode);
		state.showLightPen = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80.0f, 0)))
		state.showLightPen = false;

	ImGui::End();
}
