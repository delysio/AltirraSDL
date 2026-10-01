#pragma once

#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>

class ATJoystickManagerSDL3;

// Main-thread capture shared by guided mapping workflows. Physical input
// events are intercepted before accelerators, virtual keyboard and ImGui.
namespace ATInputCapture {
struct Result {
	uint32 code = 0;
	int unit = -1; // keyboard = -1
};

void Start(bool keyboard, bool controller, int unit = -1);
void Stop();
// Restore UI navigation only after controls held during cancellation release.
void Update(ATJoystickManagerSDL3 *manager);
void Shutdown();
bool IsActive();
bool HandleEvent(const SDL_Event& event);
bool Poll(ATJoystickManagerSDL3 *manager, Result& result);
bool WasCancelled();
const char *GetPrompt();
}
