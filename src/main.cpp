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

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/version.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

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
  const size_t comment = value.find_first_of("#;");
  if (comment != std::string_view::npos) {
    value = value.substr(0, comment);
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
  std::filesystem::path path = Trim(value);
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
       << "# Output effect: bilinear, cas, fsr, fsr2, fsr3\n"
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

}  // namespace

class FutariApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<FutariApp>(new FutariApp(ctx, "futari", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    LoadFutariIni(exe_dir, paths);
    // Default the game data root to an "assets" folder next to the executable
    // when no --game_data_root was provided, so the game runs on a plain
    // double-click (no launcher .bat needed). An explicit --game_data_root
    // (cvar non-empty) still takes precedence.
    if (paths.game_data_root.empty()) {
      paths.game_data_root = exe_dir / "assets";
    }
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    (void)config;
    // Non-game runtime settings are baked into the binary (not exposed in the
    // .ini, which carries only user-facing game settings): a full content
    // license, and keyboard/mouse input on with the mouse left uncaptured.
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

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dlc_dir, ec)) {
      if (ec || !entry.is_regular_file(ec)) {
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

REX_DEFINE_APP(futari, FutariApp::Create)
