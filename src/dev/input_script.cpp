// futari_recomp (2026-11-04, leg/rectlist): see input_script.h.
#include "dev/input_script.h"

#include <rex/logging.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <vector>

namespace futari::dev {
namespace {

struct Press {
  int at_ms = 0;
  int hold_ms = 100;
  SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
  std::string name;
};

SDL_GamepadButton ButtonByName(const std::string& name) {
  static const struct {
    const char* name;
    SDL_GamepadButton button;
  } kButtons[] = {
      {"a", SDL_GAMEPAD_BUTTON_SOUTH},          {"b", SDL_GAMEPAD_BUTTON_EAST},
      {"x", SDL_GAMEPAD_BUTTON_WEST},           {"y", SDL_GAMEPAD_BUTTON_NORTH},
      {"start", SDL_GAMEPAD_BUTTON_START},      {"back", SDL_GAMEPAD_BUTTON_BACK},
      {"up", SDL_GAMEPAD_BUTTON_DPAD_UP},       {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
      {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},   {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
      {"lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
  };
  for (const auto& b : kButtons) {
    if (name == b.name) {
      return b.button;
    }
  }
  return SDL_GAMEPAD_BUTTON_INVALID;
}

bool Parse(const std::string& script, std::vector<Press>& out) {
  std::stringstream all(script);
  std::string item;
  while (std::getline(all, item, ',')) {
    if (item.empty()) {
      continue;
    }
    std::stringstream fields(item);
    std::string at, name, hold;
    std::getline(fields, at, ':');
    std::getline(fields, name, ':');
    std::getline(fields, hold, ':');
    Press p;
    char* end = nullptr;
    p.at_ms = int(std::strtol(at.c_str(), &end, 10));
    if (at.empty() || *end) {
      REXLOG_ERROR("futari_input_script: bad time in '{}'", item);
      return false;
    }
    if (!hold.empty()) {
      p.hold_ms = int(std::strtol(hold.c_str(), &end, 10));
      if (*end || p.hold_ms <= 0) {
        REXLOG_ERROR("futari_input_script: bad hold in '{}'", item);
        return false;
      }
    }
    p.button = ButtonByName(name);
    p.name = name;
    if (p.button == SDL_GAMEPAD_BUTTON_INVALID) {
      REXLOG_ERROR("futari_input_script: unknown button '{}' (a b x y start back up down left right lb rb)", name);
      return false;
    }
    out.push_back(p);
  }
  return !out.empty();
}

std::thread g_thread;
std::atomic<bool> g_stop{false};

// Sleeps until `until`; false when asked to stop first.
bool SleepUntil(std::chrono::steady_clock::time_point until) {
  while (std::chrono::steady_clock::now() < until) {
    if (g_stop.load(std::memory_order_acquire)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return !g_stop.load(std::memory_order_acquire);
}

void Run(std::vector<Press> presses, std::chrono::steady_clock::time_point start) {
  // The SDK's driver inits SDL's gamepad subsystem on the UI thread once the window exists.
  while (!(SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD)) {
    if (!SleepUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(50))) {
      return;
    }
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
  desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  desc.name = "futari_input_script";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  SDL_Joystick* joystick = id ? SDL_OpenJoystick(id) : nullptr;
  if (!joystick) {
    REXLOG_ERROR("futari_input_script: no virtual gamepad: {}", SDL_GetError());
    return;
  }
  REXLOG_INFO("futari_input_script: virtual gamepad attached (id {}), {} presses", id, presses.size());
  for (const Press& p : presses) {
    if (!SleepUntil(start + std::chrono::milliseconds(p.at_ms))) {
      break;
    }
    REXLOG_INFO("futari_input_script: {} ms: {} down ({} ms)", p.at_ms, p.name, p.hold_ms);
    SDL_SetJoystickVirtualButton(joystick, p.button, true);
    if (!SleepUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(p.hold_ms))) {
      SDL_SetJoystickVirtualButton(joystick, p.button, false);
      break;
    }
    SDL_SetJoystickVirtualButton(joystick, p.button, false);
  }
  REXLOG_INFO("futari_input_script: done");
  // The pad stays attached (detaching it would read as an unplugged controller); SDL drops it at exit.
}

}  // namespace

bool StartInputScript(const std::string& script) {
  std::vector<Press> presses;
  if (!Parse(script, presses)) {
    REXLOG_ERROR("futari_input_script: '{}' does not parse; off", script);
    return false;
  }
  g_stop.store(false, std::memory_order_release);
  g_thread = std::thread(Run, std::move(presses), std::chrono::steady_clock::now());
  return true;
}

void StopInputScript() {
  g_stop.store(true, std::memory_order_release);
  if (g_thread.joinable()) {
    g_thread.join();
  }
}

}  // namespace futari::dev
