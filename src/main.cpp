// futari - ReXGlue Recompiled Project
// Mushihimesama Futari Ver. 1.5 (Xbox 360) - Title ID 435A07D7
//
// A `futari.ini` next to the executable exposes display / graphics / storage /
// diagnostics settings (parsed in OnConfigurePaths, before the runtime/window
// are created). Storage paths default to the platform folder
// (Documents\futari on Windows); leave them empty to keep that, or set them to
// relocate saves/cache. Relative storage paths resolve next to the .exe, which
// is handy for a portable build.

#include "generated/default/futari_init.h"

#include "ini_merge.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/version.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::string Trim(std::string_view value) {
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string StripInlineComment(std::string_view value) {
  // A '#' or ';' starts an inline comment only when it is the first non-space
  // character of the line, or is immediately preceded by whitespace. This keeps
  // embedded '#'/';' inside unquoted values/paths (e.g. C:\Users\me#1\saves)
  // while still stripping full-line comments and trailing " # ..." comments.
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  size_t i = 0;
  while (i < value.size() && is_space(static_cast<unsigned char>(value[i]))) {
    ++i;
  }
  for (; i < value.size(); ++i) {
    const char c = value[i];
    if (c == '#' || c == ';') {
      // i is either the first non-space char (start of left-trimmed content)
      // or there is a preceding char; treat as a comment when that preceding
      // char is whitespace.
      const bool at_content_start = i == 0 ||
                                    is_space(static_cast<unsigned char>(value[i - 1]));
      if (at_content_start) {
        value = value.substr(0, i);
        break;
      }
    }
  }
  return Trim(value);
}

std::optional<int32_t> ParseInt(std::string_view value) {
  const std::string text = Trim(value);
  int32_t out = 0;
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return out;
}

std::optional<double> ParseDouble(std::string_view value) {
  try {
    size_t parsed = 0;
    const std::string text = Trim(value);
    double out = std::stod(text, &parsed);
    if (parsed != text.size()) {
      return std::nullopt;
    }
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<bool> ParseBool(std::string_view value) {
  const std::string lowered = Lower(Trim(value));
  if (lowered == "true" || lowered == "yes" || lowered == "on" || lowered == "1") {
    return true;
  }
  if (lowered == "false" || lowered == "no" || lowered == "off" || lowered == "0") {
    return false;
  }
  return std::nullopt;
}

std::optional<std::pair<int32_t, int32_t>> ParseResolution(std::string_view value) {
  std::string text = Lower(Trim(value));
  const size_t sep = text.find('x');
  if (sep == std::string::npos) {
    return std::nullopt;
  }
  auto width = ParseInt(text.substr(0, sep));
  auto height = ParseInt(text.substr(sep + 1));
  if (!width || !height || *width <= 0 || *height <= 0) {
    return std::nullopt;
  }
  return std::pair<int32_t, int32_t>(*width, *height);
}

int32_t ClampInt(int32_t value, int32_t min, int32_t max) {
  return std::clamp(value, min, max);
}

std::filesystem::path ResolveIniPath(const std::filesystem::path& exe_dir, std::string_view value) {
  std::string text = Trim(value);
  // Strip one layer of matching quotes. The SDK's own parser for this same file
  // tolerates quoted values (cvar.cpp Unquote()), and the defaults written below
  // model the quoted form -- without this, `user_data_path = "D:\Futari Saves"`
  // keeps its quotes, fails is_absolute(), and resolves to an uncreatable
  // <exe_dir>\"D:\Futari Saves".
  if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                           (text.front() == '\'' && text.back() == '\''))) {
    text = text.substr(1, text.size() - 2);
  }
  std::filesystem::path path = text;
  if (path.empty()) {
    return {};
  }
  return path.is_absolute() ? path : exe_dir / path;
}

void SetCvar(std::string_view name, std::string_view value) {
  rex::cvar::SetFlagByName(name, value);
}

void SetCvarBool(std::string_view name, bool value) {
  SetCvar(name, value ? "true" : "false");
}

void SetCvarInt(std::string_view name, int32_t value) {
  SetCvar(name, std::to_string(value));
}

void SetCvarDouble(std::string_view name, double value) {
  SetCvar(name, std::to_string(value));
}

void WriteDefaultIni(const std::filesystem::path& path) {
  if (std::filesystem::exists(path)) {
    return;
  }
  std::ofstream file(path);
  if (!file) {
    return;
  }
  file << "# Mushihimesama Futari (recompiled) PC settings\n"
       << "# Edit this file, then restart the game. Values shown are the defaults.\n"
       << "# The in-game settings overlay (F4 -> \"Save to config\") saves into this\n"
       << "# same file: it updates the settings lines in place and keeps your\n"
       << "# comments. Overlay settings with no line here go into a marked block\n"
       << "# at the top of the file.\n"
       << "\n"
       << "[Display]\n"
       << "# Window size as WxH. The game opens windowed at this size; the guest\n"
       << "# still renders at its native resolution and is scaled to fit.\n"
       << "resolution = 1920x1080\n"
       << "fullscreen = false\n"
       << "vsync = true\n"
       << "refresh_rate = 60\n"
       << "letterbox = true\n"
       << "\n"
       << "[Graphics]\n"
       << "# Output effect: bilinear (cas, fsr, fsr2, fsr3 require a\n"
       << "# FidelityFX-enabled build, which this one is not)\n"
       << "upscaler = bilinear\n"
       << "cas_sharpness = 0.0\n"
       << "dither = false\n"
       << "\n"
       << "[Storage]\n"
       << "# Where saves and shader cache go. Empty = platform folder\n"
       << "# (Documents\\futari). A relative path resolves next to the .exe\n"
       << "# (e.g. \"saves\" / \"cache\") for a portable build.\n"
       << "user_data_path =\n"
       << "cache_path =\n"
       << "\n"
       << "[Diagnostics]\n"
       << "# trace, debug, info, warn, error, critical, off\n"
       << "log_level = info\n";
}

std::unordered_map<std::string, std::string> ReadIni(const std::filesystem::path& path) {
  std::unordered_map<std::string, std::string> values;
  std::ifstream file(path);
  if (!file) {
    return values;
  }
  std::string section;
  std::string line;
  while (std::getline(file, line)) {
    line = StripInlineComment(line);
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = Lower(Trim(std::string_view(line).substr(1, line.size() - 2)));
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = Lower(Trim(line.substr(0, eq)));
    std::string value = Trim(line.substr(eq + 1));
    if (!section.empty()) {
      key = section + "." + key;
    }
    values[std::move(key)] = std::move(value);
  }
  return values;
}

// Reads futari.ini (creating it with defaults on first run) and applies it:
// display/graphics/diagnostics go to cvars (read later during setup), while
// storage paths are written directly into PathConfig (the user_data_root /
// cache_root cvars were already consumed to build the defaults passed in here).
void LoadFutariIni(const std::filesystem::path& exe_dir, rex::PathConfig& paths) {
  const std::filesystem::path ini_path = exe_dir / "futari.ini";
  WriteDefaultIni(ini_path);
  const auto values = ReadIni(ini_path);

  auto find = [&](const char* key) -> const std::string* {
    auto it = values.find(key);
    return it != values.end() ? &it->second : nullptr;
  };

  if (auto* v = find("display.resolution")) {
    if (auto res = ParseResolution(*v)) {
      // Window only: size the host window, leave the guest video mode at its
      // native resolution (the output is scaled to fit the window).
      SetCvarInt("window_width", ClampInt(res->first, 640, 8192));
      SetCvarInt("window_height", ClampInt(res->second, 480, 8192));
    }
  }
  if (auto* v = find("display.fullscreen")) {
    if (auto b = ParseBool(*v))
      SetCvarBool("fullscreen", *b);
  }
  if (auto* v = find("display.vsync")) {
    if (auto b = ParseBool(*v))
      SetCvarBool("vsync", *b);
  }
  if (auto* v = find("display.refresh_rate")) {
    if (auto d = ParseDouble(*v)) {
      SetCvarDouble("video_mode_refresh_rate", std::clamp(*d, 24.0, 240.0));
    }
  }
  if (auto* v = find("display.letterbox")) {
    if (auto b = ParseBool(*v))
      SetCvarBool("present_letterbox", *b);
  }

  if (auto* v = find("graphics.upscaler")) {
    SetCvar("present_effect", Lower(Trim(*v)));
  }
  if (auto* v = find("graphics.cas_sharpness")) {
    if (auto d = ParseDouble(*v)) {
      SetCvarDouble("present_cas_additional_sharpness", std::clamp(*d, 0.0, 1.0));
    }
  }
  if (auto* v = find("graphics.dither")) {
    if (auto b = ParseBool(*v))
      SetCvarBool("present_dither", *b);
  }

  if (auto* v = find("storage.user_data_path")) {
    if (auto path = ResolveIniPath(exe_dir, *v); !path.empty()) {
      paths.user_data_root = path;
    }
  }
  if (auto* v = find("storage.cache_path")) {
    if (auto path = ResolveIniPath(exe_dir, *v); !path.empty()) {
      paths.cache_root = path;
    }
  }

  if (auto* v = find("diagnostics.log_level")) {
    SetCvar("log_level", Lower(Trim(*v)));
  }
}

std::string GetCvarByName(std::string_view name) {
  for (const auto& entry : rex::cvar::GetRegistry()) {
    if (entry.name == name && entry.getter) {
      return entry.getter();
    }
  }
  return {};
}

// "60.000000" -> "60", "0.500000" -> "0.5"; integers pass through unchanged.
std::string TidyNumber(std::string text) {
  if (text.find('.') == std::string::npos) {
    return text;
  }
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text;
}

// The overlay's "Save to config" (F4). Writes futari.ini itself, but as a
// merge, not the SDK's flat rewrite: the friendly section lines the player
// edits are updated in place (comments intact, [Storage] untouched), and any
// other overlay-modified cvars go into a delimited top-level block that the
// SDK's LoadConfig reads back on the next launch.
void SaveFutariIni(const std::filesystem::path& exe_dir) {
  const std::filesystem::path ini_path = exe_dir / "futari.ini";
  WriteDefaultIni(ini_path);  // ensure the sectioned skeleton exists

  std::string original;
  {
    std::ifstream in(ini_path, std::ios::binary);
    if (in) {
      std::ostringstream buf;
      buf << in.rdbuf();
      original = buf.str();
    }
  }

  // Friendly lines: the reverse of the LoadFutariIni mapping above. A cvar
  // that is absent from this build (e.g. present_cas_additional_sharpness only
  // exists in FidelityFX-enabled builds) yields an empty string -- skip it so
  // the player's line is left alone rather than blanked.
  std::vector<ini_merge::SectionUpdate> updates;
  const auto add = [&](const char* section, const char* key, std::string value) {
    if (!value.empty()) {
      updates.push_back({section, key, std::move(value)});
    }
  };
  {
    const std::string w = GetCvarByName("window_width");
    const std::string h = GetCvarByName("window_height");
    if (!w.empty() && !h.empty()) {
      add("Display", "resolution", w + "x" + h);
    }
  }
  add("Display", "fullscreen", GetCvarByName("fullscreen"));
  add("Display", "vsync", GetCvarByName("vsync"));
  add("Display", "refresh_rate", TidyNumber(GetCvarByName("video_mode_refresh_rate")));
  add("Display", "letterbox", GetCvarByName("present_letterbox"));
  add("Graphics", "upscaler", GetCvarByName("present_effect"));
  add("Graphics", "cas_sharpness", TidyNumber(GetCvarByName("present_cas_additional_sharpness")));
  add("Graphics", "dither", GetCvarByName("present_dither"));
  add("Diagnostics", "log_level", GetCvarByName("log_level"));

  // Everything else the overlay changed goes into the managed block -- except
  // cvars the friendly lines above already carry, and the ones OnConfigurePaths
  // / OnPreSetup derive or pin every boot (machine-local paths, forced values):
  // persisting those is dead weight at best and breaks portability at worst.
  static constexpr std::string_view kExcluded[] = {
      // carried by the friendly section lines
      "window_width", "window_height", "fullscreen", "vsync",
      "video_mode_refresh_rate", "present_letterbox", "present_effect",
      "present_cas_additional_sharpness", "present_dither", "log_level",
      // pinned in OnPreSetup or derived from PathConfig at startup
      "license_mask", "mnk_mode", "mnk_mouse_capture", "gpu_plugin",
      "game_data_root", "user_data_root", "cache_root", "update_data_root",
      "metadata_root"};

  std::string raw_block;
  {
    std::istringstream all(rex::cvar::SerializeToTOML());  // "name = value" per modified flag
    std::string line;
    while (std::getline(all, line)) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) {
        continue;
      }
      const std::string key = Trim(std::string_view(line).substr(0, eq));
      const bool excluded = std::any_of(std::begin(kExcluded), std::end(kExcluded),
                                        [&](std::string_view e) { return e == key; });
      if (!excluded) {
        raw_block += line;
        raw_block += '\n';
      }
    }
  }

  const std::string merged = ini_merge::Merge(original, updates, raw_block);

  // Write via a sibling temp file + rename so a crash mid-write cannot leave a
  // truncated futari.ini (the exact failure mode this save path used to have).
  const std::filesystem::path tmp_path = exe_dir / "futari.ini.tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      REXLOG_ERROR("Settings save: cannot open {}", tmp_path.string());
      return;
    }
    out << merged;
  }
  std::error_code ec;
  std::filesystem::rename(tmp_path, ini_path, ec);
  if (ec) {
    REXLOG_ERROR("Settings save: rename to {} failed: {}", ini_path.string(), ec.message());
    return;
  }
  REXLOG_INFO("Settings saved to {}", ini_path.string());
}

// Title-screen exit (PC quality-of-life).
//
// On the console the idle title screen accepts only START/A. Like CAVE's own PC
// ports, the port also lets the player quit from there: B on any pad, or Escape
// on the keyboard (rebindable as bind_title_exit). A hint is drawn while the
// title is idle.
//
// "Idle title" is the title scene's per-frame state function that blinks PRESS
// START BUTTON and polls for START (sub_821CBFF0, installed in the scene's
// next-state pointer at +72). Both routes to the title -- boot, and Main Menu ->
// Exit -- run through it, and it stops running as soon as START is accepted.
// It is wrapped at the bottom of this file.
constexpr uint32_t kTitleIdleState = 0x821CBFF0;
constexpr uint32_t kTitleStateOffset = 72;
// The idle state runs every frame; if it has not run for this long, the title
// is gone (or hidden behind another scene) and the hint comes down.
constexpr std::chrono::milliseconds kTitleIdleTimeout{250};

// steady_clock time of the most recent idle-title frame; 0 when not on it.
std::atomic<int64_t> g_title_idle_ns{0};
std::atomic<bool> g_title_hint_shown{false};
std::atomic<bool> g_exit_requested{false};
// Whether an interactive SDK overlay (F4 settings, console, ...) is up; while
// it is, B and Escape belong to the overlay, not to the title. Refreshed every
// frame by the hint dialog, which exists exactly while the title is idle.
std::atomic<bool> g_overlay_open{false};
// Guest-thread only: pad buttons as of the previous idle-title frame, so B
// fires on a fresh press and not on a B still held from the menu.
std::array<uint16_t, 4> g_title_prev_buttons{};

int64_t SteadyNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool TitleScreenIdle() {
  const int64_t last = g_title_idle_ns.load(std::memory_order_acquire);
  return last != 0 && SteadyNowNs() - last < std::chrono::nanoseconds(kTitleIdleTimeout).count();
}

std::string TitleExitKeyName() {
  const std::string key = rex::cvar::Query<std::string>("bind_title_exit");
  return key == "Escape" ? "Esc" : key;
}

// True if any interactive ImGui window is showing. The SDK's interactive
// overlays (settings, console, debug, achievements) are ordinary windows, while
// its passive ones (toasts, achievement pop-ups, the watermark) and the hint
// below are NoInputs. UI thread, inside an ImGui frame. The public ImGui API
// cannot enumerate windows, hence imgui_internal.h. Windows drawn later in the
// current frame are covered by WasActive (last frame).
bool InteractiveOverlayOpen() {
  const ImGuiContext* context = ImGui::GetCurrentContext();
  if (!context) {
    return false;
  }
  for (const ImGuiWindow* window : context->Windows) {
    if ((window->Active || window->WasActive) && !window->Hidden && !window->IsFallbackWindow &&
        !(window->Flags & ImGuiWindowFlags_NoInputs)) {
      return true;
    }
  }
  return false;
}

// Bottom-right hint, shown only while the title is idle. It deletes itself
// (ImGuiDialog::Close) once the title goes away, so no dialog -- and no
// continuous UI repaint -- lingers for the rest of the session.
class TitleExitHintDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit TitleExitHintDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!TitleScreenIdle() || g_exit_requested.load(std::memory_order_acquire)) {
      Close();
      return;
    }
    const bool overlay_open = InteractiveOverlayOpen();
    g_overlay_open.store(overlay_open, std::memory_order_release);
    if (overlay_open) {
      return;  // exiting is off while an overlay is up, so hide the hint too
    }
    // Re-read the bind each frame so a rebind in the F4 overlay shows at once.
    std::string key = TitleExitKeyName();
    if (text_.empty() || key != key_) {
      key_ = std::move(key);
      text_ = key_.empty() ? "Press B to exit." : "Press B or " + key_ + " to exit.";
    }
    constexpr float kMargin = 16.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kMargin, io.DisplaySize.y - kMargin),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##futari_title_exit_hint", nullptr, kFlags)) {
      ImGui::TextUnformatted(text_.c_str());
    }
    ImGui::End();
  }

  void OnClose() override { g_title_hint_shown.store(false, std::memory_order_release); }

 private:
  std::string key_;
  std::string text_;
};

}  // namespace

class FutariApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<FutariApp>(new FutariApp(ctx, "futari", PPCImageConfig));
  }

  // Called on the guest thread for every idle-title frame (see the wrapper at
  // the bottom of this file). still_idle is false on the frame START was
  // accepted, i.e. the title is being left.
  static void OnTitleIdleFrame(bool still_idle) {
    if (!still_idle) {
      g_title_idle_ns.store(0, std::memory_order_release);
      return;
    }
    const int64_t now = SteadyNowNs();
    const int64_t last = g_title_idle_ns.exchange(now, std::memory_order_acq_rel);
    const bool entered =
        last == 0 || now - last >= std::chrono::nanoseconds(kTitleIdleTimeout).count();

    FutariApp* app = instance_;
    if (!app) {
      return;
    }
    using rex::X_RESULT;  // X_ERROR_SUCCESS expands to an unqualified X_RESULT
    bool b_pressed = false;
    auto* runtime = app->runtime();
    auto* input =
        runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
    for (uint32_t user = 0; input && user < g_title_prev_buttons.size(); ++user) {
      rex::input::X_INPUT_STATE state = {};
      const uint16_t buttons =
          input->GetState(user, &state) == X_ERROR_SUCCESS ? uint16_t(state.gamepad.buttons) : 0;
      const uint16_t newly_down = buttons & ~g_title_prev_buttons[user];
      g_title_prev_buttons[user] = buttons;
      // On the first idle frame only record what is held, so a B carried over
      // from the menu does not quit the moment the title appears. The SDK keeps
      // feeding pads to the game while an overlay is open, so check for one.
      if (!entered && (newly_down & rex::input::X_INPUT_GAMEPAD_B) &&
          !g_overlay_open.load(std::memory_order_acquire)) {
        b_pressed = true;
      }
    }
    if (b_pressed) {
      app->RequestExit("B");
    } else if (entered) {
      app->ShowTitleExitHint();
    }
  }

 protected:
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    instance_ = this;
    rex::ui::RegisterBind(
        "bind_title_exit", "Escape", "Exit the game from the title screen", [this] {
          // Leave Escape to an open overlay (it may be using it itself).
          if (TitleScreenIdle() && !g_overlay_open.load(std::memory_order_acquire)) {
            RequestExit(TitleExitKeyName());
          }
        });
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    LoadFutariIni(exe_dir, paths);
    // One config file. config_path points at futari.ini itself: the SDK's
    // LoadConfig reads only top-level (unsectioned) lines -- i.e. the managed
    // overlay block -- while the sections were parsed above by LoadFutariIni.
    // F4 "Save to config" routes through OnSaveSettings below, which merges
    // into this file instead of the SDK's truncating flat rewrite (the
    // v1.4.2-era split into a separate futari_cvars.ini is retired).
    paths.config_path = exe_dir / "futari.ini";
    // One-time migration: load a leftover futari_cvars.ini (its values are
    // superseded by futari.ini's overlay block once the next F4 save runs),
    // then park it as .bak so it stops shadowing the single-file world.
    const std::filesystem::path legacy_cvars = exe_dir / "futari_cvars.ini";
    if (std::filesystem::exists(legacy_cvars)) {
      rex::cvar::LoadConfig(legacy_cvars);
      std::error_code ec;
      std::filesystem::rename(legacy_cvars, exe_dir / "futari_cvars.ini.bak", ec);
    }
    // Default the game data root to an "assets" folder next to the executable
    // when no --game_data_root was provided, so the game runs on a plain
    // double-click (no launcher .bat needed). An explicit --game_data_root
    // (cvar non-empty) still takes precedence.
    if (paths.game_data_root.empty()) {
      paths.game_data_root = exe_dir / "assets";
    }
  }

  void OnSaveSettings() override {
    SaveFutariIni(rex::filesystem::GetExecutableFolder());
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Since the SDK's v0.9 GPU-plugin split an empty gpu_plugin means headless;
    // futari always renders through the Xenos plugin. An explicit --gpu_plugin
    // (cvar non-empty by the time this runs) still takes precedence.
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    // Non-game runtime settings are baked into the binary (not exposed in the
    // .ini, which carries only user-facing game settings): a full content
    // license, and keyboard/mouse input on with the mouse left uncaptured.
    // These intentionally pin the values futari relies on. They presently match
    // the SDK defaults (so they are no-ops today), but pinning them explicitly
    // insulates the port from any future drift in those SDK defaults; do not
    // remove them on the grounds that they currently look redundant.
    SetCvarInt("license_mask", 1);
    SetCvarBool("mnk_mode", true);
    SetCvarBool("mnk_mouse_capture", false);
  }

  // Auto-install any DLC packages bundled next to the exe (a "dlc" folder)
  // before the game launches, so they appear as owned marketplace content.
  // Runs after the kernel/content-manager are up but before guest code starts
  // (and after the SDK has set its default window title, which we replace
  // with the port's own version).
  void OnPreLaunchModule() override {
    if (auto* wnd = window()) {
      wnd->SetTitle("Mushihimesama Futari v" FUTARI_VERSION_STRING " " REXGLUE_BUILD_TITLE);
    }
    InstallBundledDlc();
  }

 private:
  // Close the window exactly as the title-bar close button does, so shutdown
  // takes the SDK's normal OnClosing path. Safe from any thread.
  void RequestExit(std::string_view source) {
    if (g_exit_requested.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    REXLOG_INFO("Title screen: exit requested ({})", source);
    app_context().CallInUIThread([this] {
      if (auto* wnd = window()) {
        wnd->RequestClose();
      }
    });
  }

  void ShowTitleExitHint() {
    app_context().CallInUIThread([this] {
      auto* drawer = imgui_drawer();
      if (!drawer || !TitleScreenIdle() ||
          g_title_hint_shown.exchange(true, std::memory_order_acq_rel)) {
        return;
      }
      new TitleExitHintDialog(drawer);  // owned by the drawer; deletes itself
    });
  }

  static inline FutariApp* instance_ = nullptr;

  static void InstallBundledDlc() {
    const auto dlc_dir = rex::filesystem::GetExecutableFolder() / "dlc";
    std::error_code ec;
    if (!std::filesystem::exists(dlc_dir, ec)) {
      return;
    }
    auto* kernel_state = rex::system::kernel_state();
    if (!kernel_state) {
      return;
    }
    auto* content_manager = kernel_state->content_manager();
    if (!content_manager) {
      return;
    }

    // Walk dlc/ with the std::error_code-aware increment so a mid-walk I/O
    // error can never throw (the throwing range-for operator++ would otherwise
    // std::terminate the process at launch). The constructor's ec is reused for
    // each increment(ec); on any iteration error we break out cleanly.
    std::filesystem::recursive_directory_iterator it(dlc_dir, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
      return;
    }
    for (; it != end; it.increment(ec)) {
      if (ec) {
        break;
      }
      const auto& entry = *it;
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      // Only consider STFS packages (CON / LIVE / PIRS magic at offset 0).
      char magic[4] = {};
      {
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f.read(magic, sizeof(magic))) {
          continue;
        }
      }
      const bool is_stfs = std::memcmp(magic, "LIVE", 4) == 0 ||
                           std::memcmp(magic, "CON ", 4) == 0 || std::memcmp(magic, "PIRS", 4) == 0;
      if (!is_stfs) {
        continue;
      }
      const auto name = rex::path_to_utf8(entry.path().filename());
      auto result = content_manager->InstallContent(entry.path());
      REXLOG_INFO("DLC: InstallContent('{}') -> {:#010x}", name, static_cast<uint32_t>(result));
    }
  }
};

// Wrap the idle-title state (see "Title-screen exit" above). Generated guest
// functions are weak aliases of __imp__sub_*, so this strong definition takes
// over the dispatch entry without any codegen change; the original still runs
// first, unmodified. Afterwards the scene's next-state pointer tells whether
// the title is still idle or START was just accepted.
REX_EXTERN(__imp__sub_821CBFF0);
extern "C" REX_FUNC(sub_821CBFF0) {
  const uint32_t scene = ctx.r3.u32;
  __imp__sub_821CBFF0(ctx, base);
  FutariApp::OnTitleIdleFrame(REX_LOAD_U32(scene + kTitleStateOffset) == kTitleIdleState);
}

REX_DEFINE_APP(futari, FutariApp::Create)
