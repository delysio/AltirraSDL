#pragma once
#include <SDL3/SDL.h>
#include "joystick.h"

class VDStringW;

// SDL3-specific extension to IATJoystickManager.
// Device removal and physical binding capture for the SDL frontend.
class ATJoystickManagerSDL3 : public IATJoystickManager {
public:
	virtual void CloseGamepad(SDL_JoystickID id) = 0;
	// Independent of legacy mapping/calibration capture ownership. Snapshots
	// must not overwrite the gameplay edge history.
	virtual void SetBindingCapture(bool capture) = 0;
	virtual const ATJoystickState *GetBindingStates(uint32& count) = 0;
	virtual bool GetBindingName(int unit, uint32 code, VDStringW& name) = 0;
};
