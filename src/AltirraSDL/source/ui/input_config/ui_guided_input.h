#pragma once

class ATSimulator;

// Fork extension requested by the user: a guided editor for ordinary CX40
// joystick maps. The full Windows-equivalent mapping editor remains available.
void ATUIOpenGuidedJoystickSetup(int port = 0);
bool ATUIIsGuidedJoystickSetupOpen();
void ATUIRenderGuidedJoystickSetup(ATSimulator& sim);
void ATUIShutdownGuidedJoystickSetup();
