/**
 * @file        shader_pipeline.cpp
 * @brief       See the header. The child processes are this exe's own tool modes (src/main.cpp).
 */
#include "launcher/shader_pipeline.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <thread>
#include <vector>

#include <fstream>
#include <sstream>

#include <rex/filesystem.h>
#include <rex/version.h>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#ifdef __APPLE__
#include <crt_externs.h>
#endif
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace futari::launcher {

namespace {

constexpr int kExpectedShaders = 42;  // Ver 1.5: 22 + 22 in default.xex, 2 + 2 in each of the eight cores

std::string Utf8(const std::filesystem::path& p) { return rex::path_to_utf8(p); }

void SetEnv(const char* name, const std::filesystem::path& value) {
#ifdef _WIN32
  const std::wstring wname(name, name + std::strlen(name));
  SetEnvironmentVariableW(wname.c_str(), value.empty() ? nullptr : value.c_str());
#else
  if (value.empty()) {
    unsetenv(name);
  } else {
    setenv(name, value.c_str(), 1);
  }
#endif
}

// Runs this exe with `args`, waits (honouring cancel and the timeout) and returns its exit code; -1 = could not
// start, -2 = timed out, -3 = cancelled. The child inherits the environment (SetEnv above).
int RunChild(const std::filesystem::path& exe, const std::vector<std::string>& args, int timeout_s,
             Progress& progress, std::string& error) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
#ifdef _WIN32
  std::wstring cmd = L"\"" + exe.wstring() + L"\"";
  for (const auto& a : args) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) {
      MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, w.data(), n);
    }
    // The SDK's command line parser splits on spaces outside quotes: quote every argument.
    cmd += L" \"" + w + L"\"";
  }
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
  buffer.push_back(L'\0');
  if (!CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      exe.parent_path().c_str(), &si, &pi)) {
    error = "Windows would not start " + Utf8(exe.filename()) + " (error " + std::to_string(GetLastError()) +
            "). If your antivirus blocked it, allow this folder and try again.";
    return -1;
  }
  CloseHandle(pi.hThread);
  int code = -2;
  for (;;) {
    const DWORD wait = WaitForSingleObject(pi.hProcess, 200);
    if (wait == WAIT_OBJECT_0) {
      DWORD exit_code = 0;
      GetExitCodeProcess(pi.hProcess, &exit_code);
      code = int(exit_code);
      break;
    }
    if (progress.cancel.load()) {
      TerminateProcess(pi.hProcess, 1);
      code = -3;
      break;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      TerminateProcess(pi.hProcess, 1);
      error = Utf8(exe.filename()) + " did not finish within " + std::to_string(timeout_s) + " s";
      break;
    }
  }
  CloseHandle(pi.hProcess);
  return code;
#else
  std::vector<std::string> words;
  words.push_back(exe.string());
  words.insert(words.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (auto& w : words) {
    argv.push_back(w.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawn(&pid, words[0].c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
    error = "could not start " + Utf8(exe.filename());
    return -1;
  }
  for (;;) {
    int status = 0;
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    if (progress.cancel.load()) {
      kill(pid, SIGTERM);
      waitpid(pid, &status, 0);
      return -3;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      error = Utf8(exe.filename()) + " did not finish within " + std::to_string(timeout_s) + " s";
      return -2;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
#endif
}

// The setup arguments a restarted game must not see again; `next_is_value`: the value came as its own word.
bool IsSetupArg(const std::string& a, bool& next_is_value) {
  next_is_value = false;
  if (a.rfind("--futari_setup", 0) != 0) {
    return false;
  }
  next_is_value = a.find('=') == std::string::npos && a.rfind("--futari_setup_source", 0) == 0;
  return true;
}

#ifdef _WIN32
std::string NarrowUtf8(const wchar_t* w) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
  if (n > 0) {
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  }
  return s;
}

// One argument quoted the way CommandLineToArgvW (and the SDK's parser) reads it back: backslashes before a quote
// and at the end are doubled, quotes escaped.
std::wstring QuoteArg(const std::wstring& a) {
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : a) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
    } else {
      out.append(backslashes, L'\\');
    }
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}
#endif

uint32_t LeU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t LeU64(const uint8_t* p) { return uint64_t(LeU32(p)) | (uint64_t(LeU32(p + 4)) << 32); }

}  // namespace

uint32_t RelaunchWithoutSetup(const std::filesystem::path& exe, std::string& error) {
#ifdef _WIN32
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    error = "could not read this process's command line (error " + std::to_string(GetLastError()) + ")";
    return 0;
  }
  std::wstring cmd = QuoteArg(exe.wstring());
  for (int i = 1; i < argc; ++i) {
    bool next_is_value = false;
    if (IsSetupArg(NarrowUtf8(argv[i]), next_is_value)) {
      if (next_is_value && i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) != 0) {
        ++i;
      }
      continue;
    }
    cmd += L" " + QuoteArg(argv[i]);
  }
  LocalFree(argv);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
  buffer.push_back(L'\0');
  if (!CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                      exe.parent_path().c_str(), &si, &pi)) {
    error = "Windows would not start " + Utf8(exe.filename()) + " (error " + std::to_string(GetLastError()) + ")";
    return 0;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return uint32_t(pi.dwProcessId);
#else
  std::vector<std::string> args;
#ifdef __APPLE__
  const int argc = *_NSGetArgc();
  char** argv = *_NSGetArgv();
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
#else
  {
    std::ifstream f("/proc/self/cmdline", std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t start = 0;
    bool first = true;
    for (size_t i = 0; i < all.size(); ++i) {
      if (all[i] == '\0') {
        if (!first) {
          args.push_back(all.substr(start, i - start));
        }
        first = false;
        start = i + 1;
      }
    }
  }
#endif
  std::vector<std::string> words{exe.string()};
  for (size_t i = 0; i < args.size(); ++i) {
    bool next_is_value = false;
    if (IsSetupArg(args[i], next_is_value)) {
      if (next_is_value && i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) {
        ++i;
      }
      continue;
    }
    words.push_back(args[i]);
  }
  std::vector<char*> argv_out;
  for (auto& w : words) {
    argv_out.push_back(w.data());
  }
  argv_out.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawn(&pid, words[0].c_str(), nullptr, nullptr, argv_out.data(), environ) != 0) {
    error = "could not start " + Utf8(exe.filename());
    return 0;
  }
  return uint32_t(pid);
#endif
}

bool CheckPackage(const std::filesystem::path& package, int& count, std::string& why) {
  count = 0;
  std::vector<uint8_t> data;
  {
    std::error_code ec;
    const auto size = std::filesystem::file_size(package, ec);
    if (ec) {
      why = "missing";
      return false;
    }
    data.resize(size_t(size));
#ifdef _WIN32
    FILE* f = _wfopen(package.c_str(), L"rb");
#else
    FILE* f = std::fopen(package.c_str(), "rb");
#endif
    if (!f) {
      why = "cannot open";
      return false;
    }
    const size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    data.resize(got);
  }
  if (data.size() < 32) {
    why = "too short";
    return false;
  }
  const uint8_t* h = data.data();
  if (std::memcmp(h, "BVKS", 4) != 0 || LeU32(h + 4) != 1) {
    why = "not a SPIR-V package";
    return false;
  }
  const uint32_t n = LeU32(h + 8), table = LeU32(h + 16), data_off = LeU32(h + 20);
  if (uint64_t(table) + uint64_t(n) * 56 > data.size() || data_off > data.size()) {
    why = "truncated table";
    return false;
  }
  uint64_t last = 0;
  bool first = true;
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* e = h + table + 56 * i;
    const uint64_t hash = LeU64(e);
    const uint32_t off = LeU32(e + 8), csize = LeU32(e + 12), stage = LeU32(e + 20);
    if ((!first && hash <= last) || uint64_t(data_off) + off + csize > data.size() || (stage != 0 && stage != 4)) {
      why = "entry " + std::to_string(i) + " is out of order, out of bounds or of an unknown stage";
      return false;
    }
    last = hash;
    first = false;
  }
  count = int(n);
  return true;
}

bool PackageStatus(const std::filesystem::path& exe_dir, int& count, uint64_t& bytes) {
  std::string why;
  const auto package = exe_dir / "vulkan_spirv.bin";
  std::error_code ec;
  bytes = std::filesystem::is_regular_file(package, ec) ? std::filesystem::file_size(package, ec) : 0;
  return CheckPackage(package, count, why);
}

#ifdef _WIN32
constexpr const char* kDxcompilerName = "dxcompiler.dll";
#else
constexpr const char* kDxcompilerName = "libdxcompiler.dylib";
#endif

// renut_tools/ beside the exe; in a macOS bundle, where Contents/MacOS may hold only Mach-O files (the code seal), the
// header is in Contents/Resources/renut_tools and the dylib in Contents/lib (mac-build/build_app.sh).
std::filesystem::path ToolFile(const std::filesystem::path& exe_dir, const std::string& name) {
  std::error_code ec;
  const auto beside = exe_dir / "renut_tools" / name;
#ifdef __APPLE__
  if (!std::filesystem::is_regular_file(beside, ec)) {
    const auto bundled = name == kDxcompilerName ? exe_dir / ".." / "lib" / name
                                                 : exe_dir / ".." / "Resources" / "renut_tools" / name;
    if (std::filesystem::is_regular_file(bundled, ec)) {
      return bundled.lexically_normal();
    }
  }
#endif
  return beside;
}

#ifndef FUTARI_SHADER_TRANSLATOR_HASH
#define FUTARI_SHADER_TRANSLATOR_HASH "unknown"
#endif

std::string ShaderBuildId() {
  return std::string("futari ") + FUTARI_VERSION_STRING + "; rexglue " + REXGLUE_VERSION_STRING +
         "; xenosrecomp " + FUTARI_SHADER_TRANSLATOR_HASH;
}

namespace {
constexpr const char* kStampName = "vulkan_spirv.stamp";
}

StampState CheckPackageStamp(const std::filesystem::path& exe_dir, std::string& detail) {
  std::error_code ec;
  const auto package = exe_dir / "vulkan_spirv.bin";
  if (!std::filesystem::is_regular_file(package, ec)) {
    detail = "no vulkan_spirv.bin";
    return StampState::kNoPackage;
  }
  const uint64_t bytes = std::filesystem::file_size(package, ec);
  std::ifstream in(exe_dir / kStampName, std::ios::binary);
  if (!in) {
    detail = "vulkan_spirv.bin has no vulkan_spirv.stamp beside it";
    return StampState::kUnstamped;
  }
  std::string line, build;
  uint64_t stamped_bytes = 0;
  bool have_bytes = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("build=", 0) == 0) {
      build = line.substr(6);
    } else if (line.rfind("package_bytes=", 0) == 0) {
      stamped_bytes = std::strtoull(line.c_str() + 14, nullptr, 10);
      have_bytes = true;
    }
  }
  if (build.empty() || !have_bytes || stamped_bytes != bytes) {
    detail = "vulkan_spirv.stamp does not describe this vulkan_spirv.bin (" + std::to_string(bytes) + " bytes)";
    return StampState::kUnstamped;
  }
  if (build != ShaderBuildId()) {
    detail = "vulkan_spirv.bin was made by \"" + build + "\", this is \"" + ShaderBuildId() + "\"";
    return StampState::kStale;
  }
  detail = "vulkan_spirv.bin was made by this build";
  return StampState::kCurrent;
}

bool WritePackageStamp(const std::filesystem::path& exe_dir, std::string& error) {
  std::error_code ec;
  const uint64_t bytes = std::filesystem::file_size(exe_dir / "vulkan_spirv.bin", ec);
  if (ec) {
    error = "no vulkan_spirv.bin to stamp";
    return false;
  }
  std::ofstream out(exe_dir / kStampName, std::ios::binary | std::ios::trunc);
  out << "# futari_recomp: the build that made vulkan_spirv.bin beside this file. The launcher redoes the shaders\n"
         "# when this differs from the running build (or the package's size does not match).\n"
      << "build=" << ShaderBuildId() << "\n"
      << "package_bytes=" << bytes << "\n";
  out.close();
  if (!out) {
    error = "cannot write " + Utf8(exe_dir / kStampName);
    return false;
  }
  return true;
}

bool ToolsPresent(const std::filesystem::path& exe_dir, std::string& missing) {
  std::error_code ec;
  for (const char* name : {kDxcompilerName, "shader_common.h"}) {
    if (!std::filesystem::is_regular_file(ToolFile(exe_dir, name), ec)) {
      missing = "renut_tools/" + std::string(name);
      return false;
    }
  }
  return true;
}

bool TranslatorAvailable() {
#if FUTARI_RENUT_TRANSLATOR
  return true;
#else
  return false;
#endif
}

ShaderResult MakeShaderPackage(const ShaderEnv& env, Progress& progress) {
  ShaderResult result;
  if (!TranslatorAvailable()) {
    result.message = "This build has no shader translator (Renut needs it to draw).";
    return result;
  }
  std::string missing;
  if (!ToolsPresent(env.exe_dir, missing)) {
    result.message = "no " + missing + ": the renut_tools folder that ships with the port is incomplete";
    return result;
  }
  std::error_code ec;
  const auto work = env.exe_dir / "shader_work";
  const auto dump = work / "dump";
  std::filesystem::remove_all(work, ec);
  std::filesystem::create_directories(dump, ec);
  const auto started = std::chrono::steady_clock::now();
  const std::string assets_arg = "--game_data_root=" + Utf8(env.assets);
  // The children get the parent's save folder too, so a scratch --user_data_root is honoured all the way down
  // (their logs used to say the default folder even when the parent had another; nothing was written there).
  const std::string user_arg = env.user_data.empty() ? "" : "--user_data_root=" + Utf8(env.user_data);
  bool keep_work = true;
  struct Cleanup {
    const std::filesystem::path& work;
    bool& keep;
    ~Cleanup() {
      if (!keep) {
        std::error_code e;
        std::filesystem::remove_all(work, e);
      }
    }
  } cleanup{work, keep_work};

  progress.Log("1/3  Reading the shaders out of your game's executables (the game opens and closes by itself)...");
  progress.SetFraction(0.1f);
  std::string error;
  // --futari_renderer=default: the dump must not start the Renut renderer the ini may ask for (no package yet).
  std::vector<std::string> dump_args{assets_arg, "--futari_renut_dump_shaders=" + Utf8(dump),
                                     "--futari_renderer=default"};
  if (!user_arg.empty()) dump_args.push_back(user_arg);
  int rc = RunChild(env.exe, dump_args, 180, progress, error);
  if (rc == -3) {
    result.message = "Cancelled.";
    return result;
  }
  if (rc != 0) {
    result.message = error.empty() ? Utf8(env.exe.filename()) + " could not dump the shaders (exit " +
                                         std::to_string(rc) + "; see the newest logs/*.log)"
                                   : error;
    return result;
  }
  int bins = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dump, ec)) {
    if (entry.path().extension() == ".bin") {
      ++bins;
    }
  }
  if (!std::filesystem::is_regular_file(dump / "manifest.csv", ec) || bins == 0) {
    result.message = "the dump is empty: is assets/ a complete Ver 1.5 extraction?";
    return result;
  }
  progress.Log("     " + std::to_string(bins) + " shaders" +
               (bins == kExpectedShaders ? "" : " (expected " + std::to_string(kExpectedShaders) +
                                                    ": a different game version?)"));

  progress.Log("2/3  Translating them (XenosRecomp built into the exe, DirectX Shader Compiler)...");
  progress.SetFraction(0.4f);
  const auto package_tmp = work / "vulkan_spirv.bin";
  SetEnv("XENOS_RECOMP_SPIRV_PACKAGE", package_tmp);
  std::vector<std::string> translate_args{
      assets_arg, "--futari_renut_translate=" + Utf8(dump),
      "--futari_renut_translate_out=" + Utf8(work / "cache.cpp"),
      "--futari_renut_translate_common=" + Utf8(ToolFile(env.exe_dir, "shader_common.h")),
      "--futari_renut_dxcompiler=" + Utf8(ToolFile(env.exe_dir, kDxcompilerName)), "--futari_renderer=default"};
  if (!user_arg.empty()) translate_args.push_back(user_arg);
  rc = RunChild(env.exe, translate_args, 300, progress, error);
  SetEnv("XENOS_RECOMP_SPIRV_PACKAGE", {});
  if (rc == -3) {
    result.message = "Cancelled.";
    return result;
  }
  if (rc != 0) {
    result.message = error.empty() ? "the translator failed (exit " + std::to_string(rc) +
                                         "; see the newest logs/*.log)"
                                   : error;
    return result;
  }
  if (!std::filesystem::is_regular_file(package_tmp, ec)) {
    result.message = "the translator wrote no package (is " + Utf8(env.exe.filename()) + " this version's build?)";
    return result;
  }
  int count = 0;
  std::string why;
  if (!CheckPackage(package_tmp, count, why)) {
    result.message = "the package is unusable: " + why;
    return result;
  }
  if (count != bins) {
    progress.Log("     note: " + std::to_string(bins - count) + " shader(s) did not translate; the game may show gaps");
  }

  progress.Log("3/3  vulkan_spirv.bin: " + std::to_string(count) + " shaders");
  progress.SetFraction(0.9f);
  const auto package = env.exe_dir / "vulkan_spirv.bin";
  std::filesystem::copy_file(package_tmp, package, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    result.message = "cannot write " + Utf8(package) + ": " + ec.message();
    return result;
  }
  std::string stamp_error;
  if (!WritePackageStamp(env.exe_dir, stamp_error)) {
    progress.Log("     note: " + stamp_error + " (the next start will think the shaders are stale)");
  }
  keep_work = false;
  result.ok = true;
  result.shaders = count;
  result.bytes = std::filesystem::file_size(package, ec);
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
  result.message = "Shaders ready: " + std::to_string(count) + " translated into vulkan_spirv.bin (" +
                   std::to_string(result.bytes / 1024) + " KB, " + std::to_string(seconds.count()) + " s)";
  progress.Log("[DONE] " + result.message);
  progress.SetFraction(1.0f);
  return result;
}

}  // namespace futari::launcher
