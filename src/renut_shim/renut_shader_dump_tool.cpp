/**
 * @file        renut_shader_dump_tool.cpp
 * @brief       Shader container dump from the loaded executables (see the header).
 *
 * futari_recomp (BSD 3-Clause). Port of tools/renut/renut_dump_shaders.py: same container layout (big-endian),
 * same candidate validation and overlap rule, same XXH3-64 (seed 0) key over image[h : h + virtual + physical]
 * (renut::gpu::HashShaderContainer), same names and manifest.csv.
 */

#include "renut_shader_dump_tool.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <tuple>
#include <utility>

#include <fmt/format.h>
#include <xxhash.h>

#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

namespace futari::renut_tool {
namespace {

constexpr size_t kHeaderSize = 0x24;  // sizeof(d3d::ShaderContainer)
constexpr uint32_t kCoreBase = 0x88000000;

// The game cores (default.xex, the entrypoint, is already loaded).
constexpr const char* kCores[] = {"palm_normal",  "palm_abnormal",  "reco_normal",   "reco_abnormal",
                                  "hirow_normal", "hirow_abnormal", "kiniro_normal", "kiniro_abnormal"};

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint16_t Le16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1] << 8));
}

std::string Key(const uint8_t* data, size_t size) {
  return fmt::format("{:016x}", static_cast<uint64_t>(XXH3_64bits(data, size)));
}

// OptionalHeader.ImageBase of the PE image (little-endian headers), or false.
bool PeImageBase(const uint8_t* image, size_t size, uint32_t& base) {
  if (size < 0x40 || image[0] != 'M' || image[1] != 'Z') {
    return false;
  }
  uint64_t pe = Le32(image + 0x3C);
  if (pe + 0x38 > size || std::memcmp(image + pe, "PE\0\0", 4) != 0) {
    return false;
  }
  if (Le16(image + pe + 0x18) != 0x10B) {  // PE32
    return false;
  }
  base = Le32(image + pe + 0x18 + 0x1C);
  return true;
}

// The Python's check_header(): reNut's find_shader_headers() test for a candidate at o. True: a container;
// otherwise `reason` says why not.
bool CheckHeader(const uint8_t* data, size_t size, size_t o, std::string& reason) {
  if (o % 4) {
    reason = "unaligned";
    return false;
  }
  if (uint64_t(o) + kHeaderSize > size) {
    reason = "header past the end of the image";
    return false;
  }
  const uint64_t virt = Be32(data + o + 4);
  const uint64_t phys = Be32(data + o + 8);
  const uint64_t ct = Be32(data + o + 0x10);
  const uint64_t dt = Be32(data + o + 0x14);
  const uint64_t so = Be32(data + o + 0x18);
  if (!(0x24 <= virt && virt < 0x10000)) {
    reason = fmt::format("virtual size 0x{:x} out of range", virt);
    return false;
  }
  if (!(0 < phys && phys < 0x100000)) {
    reason = fmt::format("physical size 0x{:x} out of range", phys);
    return false;
  }
  if (o + virt > size) {
    reason = "virtual part past the end of the image";
    return false;
  }
  if (ct >= virt || dt >= virt) {
    reason = fmt::format("constant/definition table offset 0x{:x}/0x{:x} not inside the virtual part 0x{:x}", ct, dt,
                         virt);
    return false;
  }
  if (!(0x24 <= so && so < virt - 8)) {
    reason = fmt::format("shader offset 0x{:x} not inside the virtual part 0x{:x}", so, virt);
    return false;
  }
  const uint64_t poff = Be32(data + o + so);
  const uint64_t psize = Be32(data + o + so + 4);
  if (poff + psize != phys) {
    reason = fmt::format("shader's physical offset 0x{:x} + size 0x{:x} != physical size 0x{:x}", poff, psize, phys);
    return false;
  }
  return true;
}

// XenosRecomp's directory scan (XenosRecomp/main.cpp) over one dumped container: the keys it would take.
std::vector<std::string> XenosRecompScan(const uint8_t* blob, size_t n) {
  std::vector<std::string> keys;
  int64_t i = 0;
  while (n > kHeaderSize && i < int64_t(n) - int64_t(kHeaderSize) - 1) {
    const uint32_t flags = Be32(blob + i);
    const uint64_t size = uint64_t(Be32(blob + i + 4)) + Be32(blob + i + 8);
    uint32_t f1c = 1;
    uint32_t f20 = 1;
    if (uint64_t(i) + kHeaderSize <= n) {
      f1c = Be32(blob + i + 0x1C);
      f20 = Be32(blob + i + 0x20);
    }
    if ((flags & 0xFFFFFF00u) == 0x102A1100u && size <= n - uint64_t(i) && f1c == 0 && f20 == 0) {
      keys.push_back(Key(blob + i, size_t(size)));
      i += int64_t(size);
    } else {
      i += 4;
    }
  }
  return keys;
}

struct ManifestRow {
  std::string name;
  std::string type;
  uint32_t virt;
  uint32_t phys;
  std::string source;
  std::string asset;
  bool operator<(const ManifestRow& o) const {
    return std::tie(name, type, virt, phys, source, asset) <
           std::tie(o.name, o.type, o.virt, o.phys, o.source, o.asset);
  }
};

struct Dump {
  std::filesystem::path out_dir;
  std::map<std::string, std::vector<uint8_t>> seen;  // name -> container
  std::vector<ManifestRow> manifest;
  DumpResult* result = nullptr;
  std::string write_error;

  void Note(std::string text) {
    REXLOG_INFO("shader dump: {}", text);
    result->notes.push_back(std::move(text));
  }

  // One module image; returns the number of containers found (accepted candidates).
  size_t Scan(const char* module, const uint8_t* image, size_t size, uint32_t xex_base, uint32_t expected_base) {
    uint32_t base = 0;
    if (!PeImageBase(image, size, base)) {
      Note(fmt::format("{}: no PE header; assuming image base 0x{:08x}", module, xex_base));
      base = xex_base;
    } else if (base != expected_base) {
      Note(fmt::format("{}: PE image base 0x{:08x}, expected 0x{:08x} (using the PE's)", module, base, expected_base));
    }
    const std::string source = fmt::format("{}_image.bin", module);

    std::vector<std::pair<size_t, std::string>> rejected;
    std::vector<size_t> headers;
    // The Python's regex b"\x10\x2a\x11": the pattern cannot overlap itself, so a plain forward scan matches it.
    for (size_t i = 0; i + 3 <= size; ++i) {
      if (image[i] == 0x10 && image[i + 1] == 0x2A && image[i + 2] == 0x11) {
        std::string why;
        if (CheckHeader(image, size, i, why)) {
          headers.push_back(i);
        } else {
          rejected.emplace_back(i, std::move(why));
        }
        i += 2;
      }
    }

    size_t found = 0;
    size_t added = 0;
    size_t n_vs = 0;
    for (size_t k = 0; k < headers.size(); ++k) {
      const size_t h = headers[k];
      const uint32_t flags = Be32(image + h);
      const uint32_t virt = Be32(image + h + 4);
      const uint32_t phys = Be32(image + h + 8);
      const uint64_t total = uint64_t(virt) + phys;
      if (h + total > size) {
        rejected.emplace_back(h, "physical part past the end of the image");
        continue;
      }
      const uint32_t f1c = Be32(image + h + 0x1C);
      const uint32_t f20 = Be32(image + h + 0x20);
      if (f1c || f20) {
        rejected.emplace_back(
            h, fmt::format("fields +0x1C/+0x20 = 0x{:x}/0x{:x} (XenosRecomp only takes zero)", f1c, f20));
        continue;
      }
      if (k + 1 < headers.size() && h + total > headers[k + 1]) {
        rejected.emplace_back(h, fmt::format("extends 0x{:x} bytes into the next container at +0x{:x}",
                                             h + total - headers[k + 1], headers[k + 1]));
        continue;
      }
      ++found;
      const bool vs = (flags & 1) != 0;
      n_vs += vs ? 1 : 0;
      const char* kind = vs ? "vs" : "ps";
      const uint8_t* blob = image + h;
      const std::string hash = Key(blob, size_t(total));
      const std::string name = fmt::format("{}_{}", kind, hash);
      const std::string asset = fmt::format("0x{:08x}", uint32_t(base + h));
      manifest.push_back({name, kind, virt, phys, source, asset});

      auto it = seen.find(name);
      if (it != seen.end()) {
        if (it->second.size() != total || std::memcmp(it->second.data(), blob, size_t(total)) != 0) {
          Note(fmt::format("{}: {} at {} has the key of a different container", module, name, asset));
        }
        continue;
      }
      // The key is the renderer's lookup key (HashShaderContainer, the same call); the scan is the key XenosRecomp
      // would file this container under.
      const auto scanned = XenosRecompScan(blob, size_t(total));
      if (scanned.size() != 1 || scanned[0] != hash) {
        Note(fmt::format("{}: {} at {}: XenosRecomp would not take it as one container", module, name, asset));
        continue;
      }
      seen.emplace(name, std::vector<uint8_t>(blob, blob + total));
      ++added;
      if (write_error.empty()) {
        const auto file = out_dir / (name + ".bin");
        std::ofstream fh(file, std::ios::binary | std::ios::trunc);
        fh.write(reinterpret_cast<const char*>(blob), std::streamsize(total));
        fh.close();
        if (!fh) {
          write_error = fmt::format("cannot write {}", file.string());
        }
      }
    }

    std::sort(rejected.begin(), rejected.end());
    Note(fmt::format("{:<16} base 0x{:08x}  {:2} vs + {:2} ps containers, {:2} new, {} rejected candidate(s)", module,
                     base, n_vs, found - n_vs, added, rejected.size()));
    for (const auto& [o, why] : rejected) {
      result->notes.push_back(fmt::format("rejected {}+0x{:06x}: {}", module, o, why));
    }
    result->rejected += rejected.size();
    return found;
  }

  bool WriteManifest() {
    std::sort(manifest.begin(), manifest.end());
    std::string text = "name,type,virtual_size,physical_size,source,asset\r\n";
    for (const auto& r : manifest) {
      text += fmt::format("{},{},{},{},{},{}\r\n", r.name, r.type, r.virt, r.phys, r.source, r.asset);
    }
    std::ofstream fh(out_dir / "manifest.csv", std::ios::binary | std::ios::trunc);
    fh.write(text.data(), std::streamsize(text.size()));
    fh.close();
    return bool(fh);
  }
};

}  // namespace

bool DumpShaders(rex::Runtime& runtime, const std::filesystem::path& out_dir, DumpResult& result,
                 std::string& error) {
  result = DumpResult{};
  error.clear();
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    error = fmt::format("cannot create {}: {}", out_dir.string(), ec.message());
    return false;
  }

  Dump dump;
  dump.out_dir = out_dir;
  dump.result = &result;

  auto* kernel = runtime.kernel_state();
  auto* memory = runtime.memory();
  if (!kernel || !memory) {
    error = "the runtime has no kernel state / memory";
    return false;
  }

  // The entrypoint (default.xex, image base 0x82000000).
  size_t entry_found = 0;
  {
    auto exe = kernel->GetExecutableModule();
    const auto* xex = exe ? exe->xex_module() : nullptr;
    if (!xex) {
      error = "no executable module is loaded";
      return false;
    }
    const uint32_t base = xex->base_address();
    const uint32_t size = xex->image_size();
    const auto* image = memory->TranslateVirtual<const uint8_t*>(base);
    if (!image || !size) {
      error = "the entrypoint image is not mapped";
      return false;
    }
    entry_found = dump.Scan("default", image, size, base, 0x82000000);
  }

  // The game cores, one at a time (they share base 0x88000000). call_entry = false: nothing of the module runs.
  for (const char* core : kCores) {
    const std::string path = fmt::format("game:\\media\\Module\\{}.xex", core);
    auto module = kernel->LoadUserModule(path, /*call_entry=*/false);
    if (!module) {
      dump.Note(fmt::format("{}: could not load {}", core, path));
      continue;
    }
    const auto* xex = module->xex_module();
    const uint32_t base = xex ? xex->base_address() : 0;
    const uint32_t size = xex ? xex->image_size() : 0;
    const auto* image = base ? memory->TranslateVirtual<const uint8_t*>(base) : nullptr;
    if (!image || !size) {
      dump.Note(fmt::format("{}: loaded but its image is not mapped", core));
    } else {
      dump.Scan(core, image, size, base, kCoreBase);
    }
    kernel->UnloadUserModule(module, /*call_entry=*/false);
  }

  if (!dump.write_error.empty()) {
    error = dump.write_error;
    return false;
  }
  if (!dump.WriteManifest()) {
    error = fmt::format("cannot write {}", (out_dir / "manifest.csv").string());
    return false;
  }

  result.occurrences = dump.manifest.size();
  result.unique = dump.seen.size();

  // Files in the folder that look like shaders but are not from this dump (XenosRecomp would compile them too).
  for (const auto& entry : std::filesystem::directory_iterator(out_dir, ec)) {
    const std::string file = entry.path().filename().string();
    const bool shaped = file.size() == 3 + 16 + 4 && (file.rfind("ps_", 0) == 0 || file.rfind("vs_", 0) == 0) &&
                        file.compare(19, 4, ".bin") == 0;
    if (shaped && !dump.seen.count(file.substr(0, 19))) {
      dump.Note(fmt::format("{} is not from this dump (XenosRecomp would compile it too)", file));
    }
  }

  REXLOG_INFO("shader dump: {} unique shaders from {} occurrences, {} rejected candidate(s) -> {}", result.unique,
              result.occurrences, result.rejected, out_dir.string());
  if (entry_found == 0) {
    error = "the entrypoint image contains no shader containers";
    return false;
  }
  return true;
}

}  // namespace futari::renut_tool
