// futari_recomp (2026-11-04, leg/rectlist): dev aid, a scripted virtual gamepad.
//
// futari_input_script = "3000:start,6000:a,8000:a" (init-only, command line): an SDL3 virtual gamepad is attached
// once the SDK's SDL input driver is up, and each listed button is pressed at that many milliseconds after the
// runtime came up (held 100 ms, or "ms:button:hold_ms"). The SDK's SDL3 driver picks the pad up through its
// SDL_EVENT_GAMEPAD_ADDED hotplug path like a real one, so the game sees real input. Empty = off.
// For gating renderers from a background shell, where keystrokes do not reach the window.
#pragma once

#include <string>

namespace futari::dev {

// Parses the script and starts its thread; false (and an error logged) when it does not parse.
bool StartInputScript(const std::string& script);
void StopInputScript();

}  // namespace futari::dev
