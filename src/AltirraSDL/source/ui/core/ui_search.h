#ifndef f_AT_UI_SEARCH_H
#define f_AT_UI_SEARCH_H
#include <imgui.h>
struct ATUIState;
struct ImGuiContext;
void ATUIOpenGlobalSearch();
void ATUIRenderSearchMenuButton();
void ATUIRenderGlobalSearch(ATUIState& state);
void ATUISearchSettingAdd(ImGuiContext *context, ImGuiID id,
	const ImVec2& min, const ImVec2& max);
const char *ATUISearchSettingLabel(ImGuiContext *context, ImGuiID id);
bool ATUIBeginSearchSettingsPage();
void ATUIEndSearchSettingsPage(bool previousHooks);
#endif
