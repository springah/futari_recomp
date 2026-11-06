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

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/overlay/settings_overlay.h>
#include <rex/version.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

// The launcher (src/launcher, 2026-11-03): the first-run setup screen when assets/default.xex is missing (the
// player's own disc image, console install or extracted folder goes into assets/ and dlc/, the Renut shaders are
// translated, then the game starts), and the settings screen (F2 in the game). No Python, no batch file.
#include "launcher/launcher_dialog.h"
#include "launcher/shader_pipeline.h"
#include "dev/input_script.h"

REXCVAR_DEFINE_BOOL(futari_setup, false, "Futari",
                    "Open the setup screen at start even when the game data is in place")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// Dev aid: take the fresh-process route after ANY extraction (the console-install route takes it by itself), to
// exercise the relaunch on a platform without a console install at hand.
REXCVAR_DEFINE_BOOL(futari_setup_fresh_process, false, "Futari",
                    "After the setup screen's extraction, always start the game in a new process (dev aid)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(futari_setup_source, "", "Futari",
                      "Setup: extract this disc image or folder at start without asking, then play (empty: off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(futari_setup_settings, false, "Futari",
                    "With futari_setup: open on the settings page instead of the setup page")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// Dev aid (src/dev/input_script.h): a scripted SDL3 virtual gamepad, "ms:button[:hold_ms],...", e.g.
//   --futari_input_script=3000:start,6000:a,8000:a
REXCVAR_DEFINE_STRING(futari_input_script, "", "Futari",
                      "Dev: press these virtual-gamepad buttons at these times, 'ms:button[:hold_ms],...' (empty: off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

#if FUTARI_RENUT_RENDERER
// Renut's Direct3D-level renderer (src/renut_engine, GPL-3.0; docs/NOTES_renut_port_scoping.md). Chosen by the
// init-only cvar futari_renderer = "renut"; every call below is gated on that, and with any other value the SDK's
// xenos plugin renders exactly as before. A distribution build vetoes the choice at boot (OnPreSetup) when its
// vulkan_spirv.bin is missing or from another build, and runs the default renderer for the session instead.
#include "renut_engine/futari_renut_vblank.h"
#include "renut_engine/gpu/banjo_renut_capture.h"
#include "renut_engine/gpu/banjo_renut_select.h"
#include "renut_engine/gpu/overlay/app_integration.h"
#include "renut_shim/renut_shader_dump_tool.h"
#include "renut_shim/renut_shader_translate_tool.h"
#include <rex/ui/immediate_drawer.h>

// Dev aid (the Renut gates): write the presented frame once, N seconds after the runtime is up, as a binary PPM
// (P6; convert with any image tool). Empty path = off. Init-only, meant for the command line:
//   --futari_renut_capture_path=out/frame.ppm --futari_renut_capture_after_s=25
REXCVAR_DEFINE_STRING(futari_renut_capture_path, "", "Futari",
                      "Renut: write the presented frame to this .ppm file once (empty: off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(futari_renut_capture_after_s, 20, "Futari",
                     "Renut: seconds after the runtime is up to take futari_renut_capture_path")
    .range(1, 600)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Tool mode for the extractor (scripts/make_shaders.py): dump the game's shader containers from the loaded
// executables into this folder (the input of XenosRecomp, which makes vulkan_spirv.bin), then exit before the game
// starts. Works with either renderer. Empty = off.
//   futari.exe --futari_renut_dump_shaders=<folder>
REXCVAR_DEFINE_STRING(futari_renut_dump_shaders, "", "Futari",
                      "Tool: dump the game's shader containers to this folder and exit (empty: off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// Tool mode for scripts/make_shaders.py, last step: write vulkan_spirv.stamp for the vulkan_spirv.bin in this folder
// (the build that made it: launcher/shader_pipeline.h) and exit, so a package made by the script reads as current
// and the launcher does not redo it with its one-pass translation. Empty = off.
//   futari.exe --futari_renut_stamp=<folder holding vulkan_spirv.bin>
REXCVAR_DEFINE_STRING(futari_renut_stamp, "", "Futari",
                      "Tool: write vulkan_spirv.stamp for the vulkan_spirv.bin in this folder and exit (empty: off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#if FUTARI_RENUT_TRANSLATOR
// Tool mode for the extractor, step 2: translate that dump to SPIR-V (Renut's XenosRecomp built into this exe, the
// DirectX Shader Compiler loaded from renut_tools/) and exit before anything of the game runs. The package and the
// early-alpha pass's two runs are asked for through the environment, as for the standalone tool (make_shaders.py sets
// XENOS_RECOMP_SPIRV_PACKAGE, BANJO_HLSL_DUMP_ONLY, BANJO_HLSL_OVERRIDE). Empty = off.
//   futari.exe --futari_renut_translate=<dump folder> [--futari_renut_translate_out=<cache .cpp>]
//              [--futari_renut_translate_common=<shader_common.h>] [--futari_renut_translate_hlsl=<HLSL dump folder>]
//              [--futari_renut_dxcompiler=<dxcompiler.dll>]
REXCVAR_DEFINE_STRING(futari_renut_translate, "", "Futari",
                      "Tool: translate this shader dump folder to SPIR-V and exit (empty: off)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(futari_renut_translate_out, "", "Futari",
                      "Tool: the shader-cache .cpp the translation writes (empty: beside the dump)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(futari_renut_translate_common, "", "Futari",
                      "Tool: shader_common.h for the translation (empty: renut_tools/ beside the exe)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(futari_renut_translate_hlsl, "", "Futari",
                      "Tool: the HLSL dump folder of the early-alpha pass (empty: none)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(futari_renut_dxcompiler, "", "Futari",
                      "Tool: dxcompiler.dll for the translation (empty: renut_tools/ beside the exe)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#endif
#endif

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
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
       << "# vsync locks the game to refresh_rate and makes the window present\n"
       << "# in step with your monitor (no tearing). On a variable refresh rate\n"
       << "# display you can keep the game at 60 but let the window present\n"
       << "# immediately again with \"present_vsync = false\" in the F4 cvar list.\n"
       << "vsync = true\n"
       << "# Renut (the Vulkan renderer, futari_renderer = renut) has its own switch:\n"
       << "# off shows each frame at once (lowest latency, tearing possible); on\n"
       << "# waits for the display's refresh, which can hitch on a panel that is\n"
       << "# not exactly 60 Hz. The F2 VSync box sets this one while Renut runs.\n"
       << "renut_vsync = false\n"
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
    if (auto b = ParseBool(*v)) {
      // Guest vblank pacing and host presentation together: "vsync = true" is
      // what players mean by no tearing. The SDK's managed overlay block is
      // loaded after this, so a saved "present_vsync = false" (VRR) still wins.
      // The SDK's `vsync` lives in the xenos plugin, which a Renut session
      // never loads: there this line reaches nothing, and Renut's own switch
      // is the renut_vsync line below (its own default: off).
      SetCvarBool("vsync", *b);
      SetCvarBool("present_vsync", *b);
    }
  }
  if (auto* v = find("display.renut_vsync")) {
    if (auto b = ParseBool(*v))
      SetCvarBool("renut_vsync", *b);
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
  // In a Renut session the SDK's `vsync` is not registered (it lives in the
  // xenos plugin): its line is left alone there, and Renut's own goes out.
  const std::string sdk_vsync = GetCvarByName("vsync");
  add("Display", "vsync", sdk_vsync);
  add("Display", "renut_vsync", GetCvarByName("renut_vsync"));
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
      "window_width", "window_height", "fullscreen", "vsync", "renut_vsync",
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
      bool excluded = std::any_of(std::begin(kExcluded), std::end(kExcluded),
                                  [&](std::string_view e) { return e == key; });
      // present_vsync normally mirrors the [Display] vsync line (the F4 VSync
      // checkbox sets both), and the managed block loads after that line, so
      // persisting it here would pin the host present to a value a later hand
      // edit of "vsync = ..." cannot change: save with VSync off, edit vsync =
      // true back in, and the window would still tear. Keep it only when it
      // deliberately differs (the VRR case: vsync = true, present_vsync = false).
      // In a Renut session (no `vsync` registered) it means nothing: drop it.
      if (!excluded && key == "present_vsync") {
        excluded = sdk_vsync.empty() || GetCvarByName("present_vsync") == sdk_vsync;
      }
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
  // The launcher's font, beside (after) the SDK's ProggyTiny, which stays the default for the F3 debug window and
  // the toasts. The F4 settings overlay draws with the launcher's font at the size the launcher picks for the
  // window (2026-11-07; it was the 10 px ProggyTiny stretched by overlay_scale, tiny beside F2).
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    futari::launcher::LauncherDialog::AddFonts(atlas);
    rex::ui::SetSettingsOverlayFontPicker(&futari::launcher::LauncherDialog::PickFont);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    instance_ = this;
    rex::ui::RegisterBind(
        "bind_title_exit", "Escape", "Exit the game from the title screen", [this] {
          // Leave Escape to an open overlay (it may be using it itself).
          // The F2 screen uses Esc as "close" (LauncherDialog::BackPressed).
          if (TitleScreenIdle() && !g_overlay_open.load(std::memory_order_acquire) &&
              !futari::launcher::LauncherDialog::instance()) {
            RequestExit(TitleExitKeyName());
          }
        });
    rex::ui::RegisterBind("bind_launcher", "F2", "Open the settings screen (renderer, display, DLC)", [this] {
      if (auto* open = futari::launcher::LauncherDialog::instance()) {
        (void)open;
        return;  // one at a time; its own Close button ends it
      }
      if (auto* drawer = imgui_drawer()) {
        auto env = MakeLauncherEnv();
        env.in_game = runtime() != nullptr;
        new futari::launcher::LauncherDialog(drawer, std::move(env),
                                             futari::launcher::LauncherDialog::Mode::kSettings);
      }
    });
  }

  // What the launcher needs to know about this install. game_data_root_ is empty before the paths are final
  // (OnFinalizePaths passes the pending root in explicitly).
  futari::launcher::LauncherDialog::Env MakeLauncherEnv(std::filesystem::path assets_dir = {}) {
    futari::launcher::LauncherDialog::Env env;
    env.exe_dir = rex::filesystem::GetExecutableFolder();
    {
      const std::string user_data = REXCVAR_GET(user_data_root);
      env.user_data_dir = std::filesystem::path(std::u8string(user_data.begin(), user_data.end()));
    }
    env.exe = rex::filesystem::GetExecutablePath();
    env.assets_dir = assets_dir.empty() ? (game_data_root().empty() ? env.exe_dir / "assets" : game_data_root())
                                        : assets_dir;
    env.dlc_dir = env.exe_dir / "dlc";
    env.version = FUTARI_VERSION_STRING;
    env.native_window = window() ? window()->GetNativeWindowHandle() : nullptr;
#if FUTARI_RENUT_RENDERER && FUTARI_RENUT_TRANSLATOR
    env.renut_available = true;
#else
    env.renut_available = false;
#endif
#if FUTARI_RENUT_RENDERER
    env.renderer_fallback = banjo::renut_gpu::FallbackReason();
#endif
    env.on_quit = [this] { RequestExit("launcher"); };
    env.save_display_settings = [] { SaveFutariIni(rex::filesystem::GetExecutableFolder()); };
    return env;
  }

  // The first-run route: with no assets/default.xex (or --futari_setup / --futari_setup_source) the setup screen
  // takes over before the SDK loads the executable, and its Play hands the paths back through `resume`.
  std::optional<rex::PathConfig> FinalizePathsThroughLauncher(const rex::PathConfig& defaults,
                                                              std::function<void(rex::PathConfig)> resume) {
    {
      // The shader package stamp (shader_pipeline.h): say so at every start when vulkan_spirv.bin is from another
      // build, since Renut then only shows shader misses. The launcher's setup flow and its Settings page redo it.
      std::string detail;
      const auto stamp = futari::launcher::CheckPackageStamp(rex::filesystem::GetExecutableFolder(), detail);
      if (futari::launcher::StampWantsRedo(stamp)) {
        REXLOG_WARN("Renut shaders are STALE: {} (F2 -> Settings -> Redo the shaders, or start with --futari_setup)",
                    detail);
      } else if (stamp == futari::launcher::StampState::kCurrent) {
        REXLOG_INFO("Renut shaders: {}", detail);
      }
    }
    const std::string auto_source = REXCVAR_GET(futari_setup_source);
    std::error_code ec;
    const bool have_game = std::filesystem::is_regular_file(defaults.game_data_root / "default.xex", ec);
    if (have_game && !REXCVAR_GET(futari_setup) && auto_source.empty()) {
      return defaults;
    }
    auto* drawer = imgui_drawer();
    if (!drawer) {
      REXLOG_WARN("Launcher: no overlay drawer, so no setup screen; the SDK reports the missing game data");
      return defaults;
    }
    REXLOG_INFO("Launcher: setup screen ({})",
                have_game ? "asked for" : "no default.xex in " + rex::path_to_utf8(defaults.game_data_root));
    auto env = MakeLauncherEnv(defaults.game_data_root);
    env.auto_source = rex::to_path(auto_source);
    const auto opened = std::chrono::steady_clock::now();
    env.on_play = [this, resume = std::move(resume), paths = defaults, opened](bool fresh_process) {
      // After an extraction from a console install the game starts in a new process: constructing the runtime in
      // this one, after the GoD/SVOD reader has hashed and copied the whole disc, ended in an access violation ~1 s
      // into the game in 3-4 of 10 runs on Windows (guest sub_821911D0, a null pointer chain; never on a plain start,
      // the loose folder or the disc image, nor with the drawer kept attached). Cause not understood.
      if (fresh_process || REXCVAR_GET(futari_setup_fresh_process)) {
        std::string error;
        if (const uint32_t pid =
                futari::launcher::RelaunchWithoutSetup(rex::filesystem::GetExecutablePath(), error)) {
          REXLOG_INFO("Launcher: restarting the game in a fresh process after the console-install extraction "
                      "(pid {})",
                      pid);
          RequestExit("restart after setup");
          return;
        }
        REXLOG_WARN("Launcher: could not restart in a fresh process ({}); starting the game in this one", error);
      }
      // Deferred: the runtime is constructed outside the ImGui frame that pressed Play. The time spent in the
      // setup screen is logged so a crash log after Play says how long the game waited for its clock (bd3f869).
      REXLOG_INFO("Launcher: Play; constructing the runtime ({} ms in the setup screen)",
                  std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - opened)
                      .count());
      app_context().CallInUIThreadDeferred([resume, paths] {
        REXLOG_INFO("Launcher: resume running");
        resume(paths);
      });
    };
    new futari::launcher::LauncherDialog(drawer, std::move(env),
                                         REXCVAR_GET(futari_setup_settings)
                                             ? futari::launcher::LauncherDialog::Mode::kSettings
                                             : futari::launcher::LauncherDialog::Mode::kSetup);
    return std::nullopt;
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

#if FUTARI_RENUT_RENDERER
  // Renut's shipped shaders are SPIR-V only (vulkan_spirv.bin beside the exe, made at extract time), so its back end
  // defaults to Vulkan here rather than its own d3d12 default. --renut_gpu_backend / the ini still win.
  void OnPostInitLogging() override {
    if (!rex::cvar::HasNonDefaultValue("renut_gpu_backend")) {
      rex::cvar::SetFlagByName("renut_gpu_backend", "vulkan");
    }
    // The stamp tool mode (make_shaders.py's last step): no window, no runtime, exit with 0 or 1.
    const std::string stamp_dir = REXCVAR_GET(futari_renut_stamp);
    if (!stamp_dir.empty()) {
      const std::filesystem::path dir(std::u8string(stamp_dir.begin(), stamp_dir.end()));
      std::string error;
      const bool ok = futari::launcher::WritePackageStamp(dir, error);
      if (!ok) {
        REXLOG_ERROR("Shader stamp FAILED: {}", error);
        std::fprintf(stderr, "Shader stamp FAILED: %s\n", error.c_str());
      } else {
        REXLOG_INFO("Shader stamp: wrote vulkan_spirv.stamp in {} ({})", stamp_dir,
                    futari::launcher::ShaderBuildId());
      }
      std::fflush(nullptr);
      std::_Exit(ok ? 0 : 1);
    }
#if FUTARI_RENUT_TRANSLATOR
    // The translate tool mode: nothing of the game is needed, so it runs here and the process ends with the tool's
    // exit code (no window, no runtime; the SDK's shutdown has nothing to undo yet).
    const std::string dump = REXCVAR_GET(futari_renut_translate);
    if (!dump.empty()) {
      // Cvar strings are UTF-8 (the SDK converts the wide command line); a path built from a std::string would
      // read them as the ANSI code page and garble a non-ASCII folder.
      const auto u8 = [](const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); };
      futari::renut_tool::TranslateArgs args;
      args.dump = u8(dump);
      args.out = u8(REXCVAR_GET(futari_renut_translate_out));
      args.common = u8(REXCVAR_GET(futari_renut_translate_common));
      args.hlsl_dump = u8(REXCVAR_GET(futari_renut_translate_hlsl));
      args.dxcompiler = u8(REXCVAR_GET(futari_renut_dxcompiler));
      REXLOG_INFO("Shader translate: {} (XenosRecomp built in)", dump);
      std::string error;
      const int rc = futari::renut_tool::Translate(args, error);
      if (!error.empty()) {
        REXLOG_ERROR("Shader translate FAILED: {}", error);
        std::fprintf(stderr, "Shader translate FAILED: %s\n", error.c_str());
      }
      REXLOG_INFO("Shader translate: exit {}", rc);
      futari::renut_tool::EndProcess(rc);
    }
#endif
  }

  // The dump tool mode: the entrypoint is loaded, nothing has run.
  void OnPostLoadXexImage() override {
    const std::string dir = REXCVAR_GET(futari_renut_dump_shaders);
    if (dir.empty()) {
      return;
    }
    futari::renut_tool::DumpResult result;
    std::string error;
    const bool ok = futari::renut_tool::DumpShaders(*runtime(), std::filesystem::path(dir), result, error);
    if (ok) {
      REXLOG_INFO("Shader dump: {} unique shaders from {} occurrences ({} rejected) written to {}", result.unique,
                  result.occurrences, result.rejected, dir);
    } else {
      REXLOG_ERROR("Shader dump FAILED: {}", error);
    }
    for (const auto& note : result.notes) {
      REXLOG_INFO("Shader dump: {}", note);
    }
    renut_dump_exit_code_ = ok ? 0 : 2;
    RequestExit("shader dump done");
  }
  int renut_dump_exit_code_ = -1;
#endif

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
#if FUTARI_RENUT_RENDERER
    // The first reading of the renderer choice. A distribution build draws the game on Renut only from
    // vulkan_spirv.bin beside the exe (shader_pipeline.h): without it, or with one from another build, Renut shows
    // a black game and only shader misses in the log, so the default renderer runs this session instead and the
    // settings screen (F2) says so. An unstamped package (a developer's make_shaders.py run) is left to Renut, with
    // FinalizePathsThroughLauncher's STALE warning.
    if (rex::cvar::Query<std::string>("futari_renderer") == "renut" && !renut::gpu::GameShadersBuiltIn()) {
      std::string detail;
      const auto stamp = futari::launcher::CheckPackageStamp(rex::filesystem::GetExecutableFolder(), detail);
      if (stamp == futari::launcher::StampState::kNoPackage || stamp == futari::launcher::StampState::kStale) {
        banjo::renut_gpu::FallBackToDefault(detail);
        REXLOG_WARN("Renderer: default (the xenos plugin) this session, not Renut (futari_renderer = renut): {}. "
                    "Renut would draw a black game without this build's shaders. F2 -> Settings -> {} the shaders, "
                    "then start the game again",
                    detail, stamp == futari::launcher::StampState::kNoPackage ? "Prepare" : "Redo");
      }
    }
    // Renut: no SDK graphics system and no GPU plugin (the SDK's "detached"
    // mode); the renderer owns the device, present timing and the overlay.
    if (banjo::renut_gpu::Active()) {
      renut::gpu::ConfigureRuntime(config);
      REXLOG_INFO("Renderer: Renut (futari_renderer = renut); the xenos plugin is not loaded");
    }
#endif
  }

#if FUTARI_RENUT_RENDERER
  // The SDK's detached-mode boot surface for Renut (the order the SDK calls
  // these: OnPreSetup, OnCreateImmediateDrawer, OnFinalizePaths, OnPostSetup).
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    if (banjo::renut_gpu::Active()) {
      return renut::gpu::CreateOverlayDrawer();
    }
    return rex::ReXApp::OnCreateImmediateDrawer();
  }

#endif

  // UI thread, the window exists, the game has not started. Renut: create the host device and start presenting
  // (overlay-only frames until the guest draws). Then the launcher's first-run route, which may hold the paths
  // back until its setup is done (either renderer draws its screen).
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) override {
#if FUTARI_RENUT_RENDERER
    if (banjo::renut_gpu::Active()) {
      if (!renut::gpu::StartRenderer(app_context(), [this] { return imgui_drawer(); })) {
        REXLOG_ERROR("Renut: StartRenderer failed; the game will run without a picture");
      }
    }
#endif
    return FinalizePathsThroughLauncher(defaults, std::move(resume));
  }

  void OnPostSetup() override {
#if FUTARI_RENUT_RENDERER
    if (banjo::renut_gpu::Active()) {
      futari::renut_vblank::Start();
      StartRenutCapture();
    }
#endif
    if (const std::string script = REXCVAR_GET(futari_input_script); !script.empty()) {
      futari::dev::StartInputScript(script);
    }
  }

#if FUTARI_RENUT_RENDERER

  // The dev capture (futari_renut_capture_path): one frame, written on its own thread.
  void StartRenutCapture() {
    const std::string path = REXCVAR_GET(futari_renut_capture_path);
    if (path.empty()) {
      return;
    }
    const int after_s = REXCVAR_GET(futari_renut_capture_after_s);
    renut_capture_thread_ = std::thread([this, path, after_s] {
      for (int i = 0; i < after_s * 10 && !renut_capture_stop_.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (renut_capture_stop_.load(std::memory_order_acquire)) {
        return;
      }
      rex::ui::RawImage image;
      if (!renut::gpu::CaptureFrame(image, 2000)) {
        REXLOG_ERROR("Renut capture: no frame within 2 s");
        return;
      }
      std::ofstream out(path, std::ios::binary);
      out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
      std::vector<char> row(size_t(image.width) * 3);
      for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t* src = image.data.data() + size_t(y) * image.stride;
        for (uint32_t x = 0; x < image.width; ++x) {
          row[size_t(x) * 3 + 0] = char(src[size_t(x) * 4 + 0]);
          row[size_t(x) * 3 + 1] = char(src[size_t(x) * 4 + 1]);
          row[size_t(x) * 3 + 2] = char(src[size_t(x) * 4 + 2]);
        }
        out.write(row.data(), std::streamsize(row.size()));
      }
      REXLOG_INFO("Renut capture: {}x{} frame at {} s written to {}", image.width, image.height, after_s, path);
    });
  }

  std::thread renut_capture_thread_;
  std::atomic<bool> renut_capture_stop_{false};

  void OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) override {
    rex::ReXApp::OnWindowPixelSizeChanged(pixel_width, pixel_height);
    if (banjo::renut_gpu::Active()) {
      renut::gpu::OnWindowResized();
    }
  }

#endif

  void OnShutdown() override {
    futari::dev::StopInputScript();
#if FUTARI_RENUT_RENDERER
    if (banjo::renut_gpu::Active()) {
      renut_capture_stop_.store(true, std::memory_order_release);
      if (renut_capture_thread_.joinable()) {
        renut_capture_thread_.join();
      }
      futari::renut_vblank::Stop();
      renut::gpu::ShutdownRenderer();
    }
#endif
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
