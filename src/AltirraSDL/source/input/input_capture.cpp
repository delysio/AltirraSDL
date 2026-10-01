#include <stdafx.h>
#include "input_capture.h"
#include "input_sdl3.h"
#include "inputdefs.h"
#include "joystick_sdl3.h"
#include <imgui.h>
#include <array>

namespace ATInputCapture {
namespace {
bool s_active = false;
bool s_keyboard = false;
bool s_controller = false;
bool s_armed = false;
bool s_captured = false;
bool s_cancelled = false;
bool s_gamepadNav = false;
bool s_restoreNav = false;
int s_unit = -1;
Result s_result;
std::array<bool, SDL_SCANCODE_COUNT> s_consumed {};
const char *s_prompt = "Release held keys and controller controls.";

bool KeyboardNeutral() {
	for (bool consumed : s_consumed) {
		if (consumed)
			return false;
	}
	int count = 0;
	const bool *keys = SDL_GetKeyboardState(&count);
	for (int i = 0; i < count; ++i) {
		if (keys[i])
			return false;
	}
	return true;
}
}

void Stop() {
	if (s_active && s_gamepadNav)
		s_restoreNav = true;
	s_active = false;
}

void Update(ATJoystickManagerSDL3 *manager) {
	if (!s_restoreNav || !KeyboardNeutral())
		return;
	uint32 count = 0;
	const ATJoystickState *states = manager ? manager->GetBindingStates(count) : nullptr;
	for (uint32 i = 0; i < count; ++i) {
		if (states[i].mButtons || states[i].mAxisButtons)
			return;
		for (sint32 axis : states[i].mDeadifiedAxisVals) {
			if (axis)
				return;
		}
	}
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	s_restoreNav = false;
}

void Shutdown() {
	Stop();
	if (s_restoreNav)
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	s_restoreNav = false;
	s_consumed.fill(false);
}

void Start(bool keyboard, bool controller, int unit) {
	Stop();
	const bool restoringNav = s_restoreNav;
	s_restoreNav = false;
	s_active = true;
	s_keyboard = keyboard;
	s_controller = controller;
	s_unit = unit;
	s_armed = false;
	s_captured = false;
	s_cancelled = false;
	s_result = {};
	s_prompt = "Release held keys and controller controls.";
	ATInputSDL3_ReleaseAllKeys();
	ImGuiIO& io = ImGui::GetIO();
	// The activation key's release is consumed during capture, so release
	// ImGui's cached keys now as well (including Enter/Space navigation).
	io.ClearInputKeys();
	s_gamepadNav = restoringNav
		|| (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
	io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
	// Drop navigation buttons already queued by ImGui's gamepad polling.
	for (int key = ImGuiKey_GamepadStart; key <= ImGuiKey_GamepadRStickDown; ++key)
		io.AddKeyEvent((ImGuiKey)key, false);
}

bool IsActive() { return s_active; }
bool WasCancelled() { return s_cancelled; }
const char *GetPrompt() { return s_prompt; }

bool HandleEvent(const SDL_Event& event) {
	if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST
		|| event.type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
		if (s_active) {
			s_cancelled = true;
			Stop();
		}
		// SDL resets keyboard state on focus loss; no releases are promised.
		s_consumed.fill(false);
		return false;
	}
	if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
		const int sc = event.key.scancode;
		if (sc < 0 || sc >= SDL_SCANCODE_COUNT)
			return s_active;
		if (!s_active) {
			if (!s_consumed[sc])
				return false;
			if (event.type == SDL_EVENT_KEY_UP)
				s_consumed[sc] = false;
			return true;
		}
		s_consumed[sc] = event.type == SDL_EVENT_KEY_DOWN;
		if (event.type == SDL_EVENT_KEY_DOWN
			&& sc == SDL_SCANCODE_ESCAPE && (event.key.mod & SDL_KMOD_SHIFT)) {
			s_cancelled = true;
			Stop();
			return true;
		}
		const bool shift = sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT;
		if (s_keyboard && s_armed && !s_captured && !event.key.repeat
			&& ((shift && event.type == SDL_EVENT_KEY_UP)
				|| (!shift && event.type == SDL_EVENT_KEY_DOWN))) {
			const uint32 code = ATInputSDL3_GetInputCode(event.key.scancode);
			if (code != kATInputCode_None) {
				s_result = { code, -1 };
				s_captured = true;
				s_prompt = "Input detected. Release it to continue.";
			} else {
				s_prompt = "This key cannot be mapped. Press another key.";
			}
		}
		return true;
	}
	if (!s_active && !s_restoreNav)
		return false;
	return event.type == SDL_EVENT_TEXT_INPUT
		|| event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
		|| event.type == SDL_EVENT_GAMEPAD_BUTTON_UP
		|| event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION
		|| event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN
		|| event.type == SDL_EVENT_JOYSTICK_BUTTON_UP
		|| event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION
		|| event.type == SDL_EVENT_JOYSTICK_HAT_MOTION;
}

bool Poll(ATJoystickManagerSDL3 *manager, Result& result) {
	if (!s_active)
		return false;
	bool neutral = KeyboardNeutral();
	uint32 count = 0;
	const ATJoystickState *states = manager && s_controller
		? manager->GetBindingStates(count) : nullptr;
	bool selectedPresent = !s_controller || s_unit < 0;
	for (uint32 i = 0; i < count; ++i) {
		const auto& state = states[i];
		if (s_unit >= 0 && (int)state.mUnit != s_unit)
			continue;
		selectedPresent = true;
		if (state.mButtons || state.mAxisButtons)
			neutral = false;
		for (sint32 axis : state.mDeadifiedAxisVals) {
			if (axis)
				neutral = false;
		}
	}
	if (!selectedPresent) {
		s_cancelled = true;
		Stop();
		return false;
	}
	if (s_captured) {
		if (!neutral)
			return false;
		result = s_result;
		Stop();
		return true;
	}
	if (!s_armed) {
		if (neutral) {
			s_armed = true;
			s_prompt = s_keyboard && s_controller
				? "Press a key or controller control (Shift+Escape cancels)."
				: s_keyboard ? "Press a key (Shift+Escape cancels)."
				: "Press a controller button or move a stick (Shift+Escape cancels).";
		}
		return false;
	}
	for (uint32 i = 0; i < count; ++i) {
		const auto& state = states[i];
		if (s_unit >= 0 && (int)state.mUnit != s_unit)
			continue;
		uint32 bits = state.mButtons;
		uint32 base = kATInputCode_JoyButton0;
		if (!bits) {
			bits = state.mAxisButtons;
			base = kATInputCode_JoyStick1Left;
		}
		if (!bits)
			continue;
		// Avoid choosing an arbitrary direction when the user moves diagonally
		// or presses several controls together.
		if ((bits & (bits - 1)) || (state.mButtons && state.mAxisButtons)) {
			s_prompt = "Release controls, then press one input or move in one direction.";
			s_armed = false;
			return false;
		}
		uint32 index = 0;
		while (!(bits & (1u << index)))
			++index;
		s_result = { base + index, (int)state.mUnit };
		s_unit = s_result.unit;
		s_captured = true;
		s_prompt = "Input detected. Release it to continue.";
		break;
	}
	return false;
}
}
