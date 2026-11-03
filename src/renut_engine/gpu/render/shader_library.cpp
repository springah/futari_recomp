/**
 * @file    gpu/render/shader_library.cpp
 */
#include "renut_engine/gpu/render/shader_library.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <smolv.h>
#include <xxhash.h>
#include <zstd.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"
#if defined(PLUME_DEKO3D)
#include "renut_engine/gpu/banjo_renut_deko3d.h"  // banjo-nx (2026-10-17): the deko3d back end's GLSL package
#endif

REXCVAR_DEFINE_STRING(renut_shader_capture_dir, "shader_capture", "Nuts&Bolts/Graphics",
                      "Where shaders missing from the shader cache are written; the next build "
                      "recompiles them into the cache");
// futari-linux: the game shaders' SPIR-V beside the executable (the web installer makes it from the player's own
// executables), for a release build that carries no translated game shader (FUTARI_LINUX_RELEASE_SHADERS).
REXCVAR_DEFINE_STRING(renut_spirv_package, "vulkan_spirv.bin", "Nuts&Bolts/Graphics",
                      "The game shaders' SPIR-V package beside the executable (made by the web installer), for a build "
                      "without them built in (a build with them built in ignores it)");
// futari-linux: writes the built-in SPIR-V as such a package and carries on, to test a release build against the
// native chain's shaders. Empty: off.
REXCVAR_DEFINE_STRING(renut_export_spirv_package, "", "Nuts&Bolts/Graphics",
                      "Write the built-in game shaders as a SPIR-V package to this path at startup (tests)");

namespace renut::gpu {

namespace {

// futari-linux: vulkan_spirv.bin, the layout of the Switch build's deko3d_glsl.bin (banjo_renut_deko3d.cpp) with
// SPIR-V in place of GLSL. The web installer's lib/vulkan.js writes it:
//   header (32 bytes): "BVKS", version 1, count, 0, table offset, data offset, 0, 0
//   table: per shader (56 bytes, sorted by hash): u64 hash, u32 offset (from the data), compressed size, size, stage
//          (0 vertex, 4 pixel), 8 x u32 bool specialization constant mask
//   data: one zstd frame per shader
#pragma pack(push, 1)
struct SpirvPackageHeader {
  char magic[4];
  uint32_t version, count, reserved0, tableOffset, dataOffset, reserved[2];
};
struct SpirvPackageEntry {
  uint64_t hash;
  uint32_t offset, compressedSize, size, stage;
  uint32_t boolMask[8];
};
#pragma pack(pop)
static_assert(sizeof(SpirvPackageHeader) == 32, "lib/vulkan.js's header");
static_assert(sizeof(SpirvPackageEntry) == 56, "lib/vulkan.js's entry");

struct SpirvPackage {
  std::once_flag once;
  bool ok = false;
  std::string path;
  std::vector<uint8_t> file;
  const SpirvPackageHeader *header = nullptr;
  const SpirvPackageEntry *entries = nullptr;
  const uint8_t *data = nullptr;
};
SpirvPackage g_package;

bool AttachSpirvPackage(SpirvPackage &p) {
  const size_t size = p.file.size();
  if (size < sizeof(SpirvPackageHeader))
    return false;
  const auto *h = reinterpret_cast<const SpirvPackageHeader *>(p.file.data());
  const uint64_t table_end = uint64_t(h->tableOffset) + uint64_t(h->count) * sizeof(SpirvPackageEntry);
  if (std::memcmp(h->magic, "BVKS", 4) != 0 || h->version != 1 || table_end > size || h->dataOffset > size) {
    RGPU_ERROR("{}: not a SPIR-V package this build reads", p.path);
    return false;
  }
  const auto *entries = reinterpret_cast<const SpirvPackageEntry *>(p.file.data() + h->tableOffset);
  for (uint32_t i = 0; i < h->count; ++i) {
    if (uint64_t(h->dataOffset) + entries[i].offset + entries[i].compressedSize > size ||
        (i && entries[i - 1].hash >= entries[i].hash)) {
      RGPU_ERROR("{}: entry {} out of bounds or out of order", p.path, i);
      return false;
    }
  }
  p.header = h;
  p.entries = entries;
  p.data = p.file.data() + h->dataOffset;
  return true;
}

void ExportBuiltInSpirvPackage(const std::string &path);

SpirvPackage &OpenSpirvPackage() {
  std::call_once(g_package.once, [] {
    SpirvPackage &p = g_package;
    if (!REXCVAR_GET(renut_export_spirv_package).empty())
      ExportBuiltInSpirvPackage(
          banjo::renut_gpu::NextToExecutable(REXCVAR_GET(renut_export_spirv_package)).string());
    p.path = banjo::renut_gpu::NextToExecutable(REXCVAR_GET(renut_spirv_package)).string();
    // futari-linux (2026-11-01): a build with the shaders built in uses only those. A 1.2.0 archive extracted over a
    // 1.1.0 folder left the installer page's package beside it, and its shaders were drawn with this build's table.
    if (g_spirvCacheCompressedSize != 0) {
      if (std::filesystem::exists(p.path))
        RGPU_INFO("{} ignored: this build has the game shaders built in (the file can be deleted)", p.path);
      return;
    }
    std::ifstream in(p.path, std::ios::binary | std::ios::ate);
    if (!in) {
      if (g_spirvCacheCompressedSize == 0)
        RGPU_ERROR("No {} and no game shaders built in: nothing of the game is drawn. Make the package with the "
                   "installer page from your own copy of the game", p.path);
      return;
    }
    const std::streamsize n = in.tellg();
    in.seekg(0);
    p.file.resize(n > 0 ? size_t(n) : 0);
    if (n <= 0 || !in.read(reinterpret_cast<char *>(p.file.data()), n)) {
      RGPU_ERROR("{}: could not be read", p.path);
      p.file.clear();
      return;
    }
    p.ok = AttachSpirvPackage(p);
    if (!p.ok)
      return;
    size_t missing = 0;
    for (size_t i = 0; i < g_shaderCacheEntryCount; ++i) {
      const uint64_t hash = g_shaderCacheEntries[i].hash;
      const SpirvPackageEntry *end = p.entries + p.header->count;
      const SpirvPackageEntry *it = std::lower_bound(
          p.entries, end, hash, [](const SpirvPackageEntry &e, uint64_t h) { return e.hash < h; });
      missing += (it != end && it->hash == hash) ? 0 : 1;
    }
    if (missing)
      RGPU_ERROR("{}: {} shaders, {} of the shader table's {} missing (made for another build?)", p.path,
                 p.header->count, missing, g_shaderCacheEntryCount);
    else
      RGPU_INFO("SPIR-V package {}: {} shaders, {} KB", p.path, p.header->count, p.file.size() >> 10);
  });
  return g_package;
}

const SpirvPackageEntry *FindPackaged(uint64_t hash) {
  SpirvPackage &p = OpenSpirvPackage();
  if (!p.ok)
    return nullptr;
  const SpirvPackageEntry *end = p.entries + p.header->count;
  const SpirvPackageEntry *it = std::lower_bound(
      p.entries, end, hash, [](const SpirvPackageEntry &e, uint64_t h) { return e.hash < h; });
  return (it != end && it->hash == hash) ? it : nullptr;
}

bool PackagedSpirv(const SpirvPackageEntry &e, std::vector<uint8_t> &spirv) {
  spirv.resize(e.size);
  const size_t n = ZSTD_decompress(spirv.data(), spirv.size(), g_package.data + e.offset, e.compressedSize);
  if (ZSTD_isError(n) || n != e.size || e.size < 20 || (e.size & 3)) {
    RGPU_ERROR("{}: shader {:016X} could not be decompressed", g_package.path, e.hash);
    return false;
  }
  return true;
}

struct DecompressedCache {
  std::once_flag once;
  std::vector<uint8_t> bytes;
};

DecompressedCache g_spirv;
#if defined(_WIN32)
DecompressedCache g_dxil;
#endif
std::atomic<uint64_t> g_decompressed_bytes{0};  // ShaderCacheHostBytes

const uint8_t *Decompress(DecompressedCache &cache, const uint8_t *compressed,
                          size_t compressed_size, size_t decompressed_size, const char *name) {
  std::call_once(cache.once, [&] {
    cache.bytes.resize(decompressed_size);
    const size_t n = ZSTD_decompress(cache.bytes.data(), decompressed_size, compressed,
                                     compressed_size);
    if (ZSTD_isError(n) || n != decompressed_size) {
      RGPU_ERROR("{} shader cache decompression failed", name);
      cache.bytes.clear();
      cache.bytes.shrink_to_fit();
    }
    g_decompressed_bytes.fetch_add(cache.bytes.capacity(), std::memory_order_relaxed);
  });
  return cache.bytes.empty() ? nullptr : cache.bytes.data();
}

std::mutex g_capture_mutex;
std::unordered_set<uint64_t> g_captured;  // CaptureMissingShader

// The built-in SPIR-V of a table entry (smol-v in the cache), decoded.
bool BuiltInSpirv(const ShaderCacheEntry &entry, std::vector<uint8_t> &spirv) {
  if (entry.spirvSize == 0)
    return false;
  const uint8_t *cache = Decompress(g_spirv, g_compressedSpirvCache, g_spirvCacheCompressedSize,
                                    g_spirvCacheDecompressedSize, "SPIR-V");
  if (!cache)
    return false;
  const uint8_t *smol = cache + entry.spirvOffset;
  spirv.resize(smolv::GetDecodedBufferSize(smol, entry.spirvSize));
  if (spirv.size() < 20 || !smolv::Decode(smol, entry.spirvSize, spirv.data(), spirv.size())) {
    RGPU_ERROR("SPIR-V decode failed for shader {:016X}", entry.hash);
    return false;
  }
  return true;
}

// futari-linux: a shader's SPIR-V, from vulkan_spirv.bin beside the executable when it has the shader, else the
// built-in cache (absent from a release build).
bool EntrySpirv(const ShaderCacheEntry &entry, std::vector<uint8_t> &spirv) {
  if (const SpirvPackageEntry *e = FindPackaged(entry.hash))
    return PackagedSpirv(*e, spirv);
  if (BuiltInSpirv(entry, spirv))
    return true;
  static std::mutex mutex;
  static std::unordered_set<uint64_t> logged;
  std::lock_guard lock(mutex);
  if (logged.insert(entry.hash).second)
    RGPU_ERROR("Shader {:016X} has no SPIR-V: not built in and not in {}", entry.hash, g_package.path);
  return false;
}

void BoolSpecMaskOf(const std::vector<uint8_t> &spirv, uint32_t mask[8]);

// One zstd frame holding `bytes` in raw blocks (RFC 8878; lib/vulkan.js writes the same): the renderer links zstd's
// decompressor only.
std::vector<uint8_t> ZstdRawFrame(const std::vector<uint8_t> &bytes) {
  constexpr size_t kMaxBlock = 128 * 1024;
  const size_t blocks = std::max<size_t>(1, (bytes.size() + kMaxBlock - 1) / kMaxBlock);
  std::vector<uint8_t> out;
  out.reserve(9 + blocks * 3 + bytes.size());
  const uint32_t magic = 0xFD2FB528u, size = uint32_t(bytes.size());
  for (int i = 0; i < 4; ++i)
    out.push_back(uint8_t(magic >> (8 * i)));
  out.push_back((2 << 6) | (1 << 5));  // 4-byte content size, single segment
  for (int i = 0; i < 4; ++i)
    out.push_back(uint8_t(size >> (8 * i)));
  for (size_t b = 0; b < blocks; ++b) {
    const size_t start = b * kMaxBlock, n = std::min(kMaxBlock, bytes.size() - start);
    const uint32_t header = (b + 1 == blocks ? 1u : 0u) | uint32_t(n << 3);  // raw block
    out.push_back(uint8_t(header));
    out.push_back(uint8_t(header >> 8));
    out.push_back(uint8_t(header >> 16));
    out.insert(out.end(), bytes.begin() + start, bytes.begin() + start + n);
  }
  return out;
}

// futari-linux (renut_export_spirv_package): the built-in SPIR-V in vulkan_spirv.bin's layout, to run a release build
// with the native chain's shaders. Game data: for tests on the machine that built it, never distributed.
void ExportBuiltInSpirvPackage(const std::string &path) {
  struct Item {
    uint64_t hash;
    uint32_t stage, size;
    uint32_t mask[8];
    std::vector<uint8_t> frame;
  };
  std::vector<Item> items;
  for (size_t i = 0; i < g_shaderCacheEntryCount; ++i) {
    const ShaderCacheEntry &entry = g_shaderCacheEntries[i];
    std::vector<uint8_t> spirv;
    if (!BuiltInSpirv(entry, spirv))
      continue;
    Item item{entry.hash, 0, uint32_t(spirv.size()), {}, {}};
    // The execution model of the module's OpEntryPoint (15): 0 vertex, 4 fragment.
    const auto *words = reinterpret_cast<const uint32_t *>(spirv.data());
    for (size_t w = 5; w < spirv.size() / 4;) {
      const uint32_t length = words[w] >> 16;
      if (length == 0)
        break;
      if ((words[w] & 0xFFFF) == 15 && w + 1 < spirv.size() / 4) {
        item.stage = words[w + 1];
        break;
      }
      w += length;
    }
    BoolSpecMaskOf(spirv, item.mask);
    item.frame = ZstdRawFrame(spirv);
    items.push_back(std::move(item));
  }
  // g_shaderCacheEntries is sorted by hash already (FindShaderEntry's lower_bound).
  SpirvPackageHeader header{};
  std::memcpy(header.magic, "BVKS", 4);
  header.version = 1;
  header.count = uint32_t(items.size());
  header.tableOffset = sizeof(SpirvPackageHeader);
  header.dataOffset = uint32_t(sizeof(SpirvPackageHeader) + items.size() * sizeof(SpirvPackageEntry));
  std::vector<SpirvPackageEntry> table(items.size());
  std::vector<uint8_t> data;
  for (size_t i = 0; i < items.size(); ++i) {
    SpirvPackageEntry &e = table[i];
    e.hash = items[i].hash;
    e.offset = uint32_t(data.size());
    e.compressedSize = uint32_t(items[i].frame.size());
    e.size = items[i].size;
    e.stage = items[i].stage;
    std::copy(items[i].mask, items[i].mask + 8, e.boolMask);
    data.insert(data.end(), items[i].frame.begin(), items[i].frame.end());
    data.resize((data.size() + 7) & ~size_t(7));
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(&header), sizeof(header));
  out.write(reinterpret_cast<const char *>(table.data()), std::streamsize(table.size() * sizeof(SpirvPackageEntry)));
  out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
  if (out)
    RGPU_INFO("renut_export_spirv_package: {} shaders written to {}", items.size(), path);
  else
    RGPU_ERROR("renut_export_spirv_package: could not write {}", path);
}

void BoolSpecMaskOf(const std::vector<uint8_t> &spirv, uint32_t mask[8]) {
  // After the five header words, each instruction's first word is (word count << 16) | opcode. OpDecorate (71):
  // target, decoration, literals; SpecId is decoration 1.
  const auto *words = reinterpret_cast<const uint32_t *>(spirv.data());
  const size_t count = spirv.size() / 4;
  for (size_t i = 5; i < count;) {
    const uint32_t length = words[i] >> 16, opcode = words[i] & 0xFFFF;
    if (length == 0)
      break;
    if (opcode == 71 && length >= 4 && i + 3 < count && words[i + 2] == 1) {
      const uint32_t id = words[i + 3];
      if (id >= 1000 && id < 1256)
        mask[(id - 1000) / 32] |= 1u << ((id - 1000) % 32);
    }
    i += length;
  }
}

} // namespace

uint64_t HashShaderContainer(const uint8_t *container) {
  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  return XXH3_64bits(container, size);
}

const ShaderCacheEntry *FindShaderEntry(uint64_t hash) {
  if (g_shaderCacheLayout != kShaderCacheLayout) {
    static bool logged = false;
    if (!logged) {
      RGPU_ERROR("Shader cache layout {} does not match the renderer's {}; rebuild it",
                 g_shaderCacheLayout, kShaderCacheLayout);
      logged = true;
    }
    return nullptr;
  }
  const ShaderCacheEntry *begin = g_shaderCacheEntries;
  const ShaderCacheEntry *end = begin + g_shaderCacheEntryCount;
  const ShaderCacheEntry *it = std::lower_bound(
      begin, end, hash, [](const ShaderCacheEntry &e, uint64_t h) { return e.hash < h; });
  return (it != end && it->hash == hash) ? it : nullptr;
}

void ShaderBoolSpecMask(const ShaderCacheEntry &entry, uint32_t mask[8]) {
  std::fill(mask, mask + 8, 0u);
#if defined(PLUME_DEKO3D)
  // banjo-nx: the GLSL package lists them (the SPIR-V cache need not be unpacked for deko3d).
  if (IsDeko3D()) {
    banjo::renut_gpu::deko3d::BoolSpecMask(entry.hash, mask);
    return;
  }
#endif
  if (!IsVulkan())
    return;
  // futari-linux: the package lists them (lib/vulkan.js reads the same decorations).
  if (const SpirvPackageEntry *e = FindPackaged(entry.hash)) {
    std::copy(e->boolMask, e->boolMask + 8, mask);
    return;
  }
  std::vector<uint8_t> spirv;
  if (BuiltInSpirv(entry, spirv))
    BoolSpecMaskOf(spirv, mask);
}

std::unique_ptr<plume::RenderShader> CreateCachedShader(plume::RenderDevice *device,
                                                        const ShaderCacheEntry &entry) {
  if (!device)
    return nullptr;

#if defined(PLUME_DEKO3D)
  // banjo-nx (2026-10-17): the deko3d back end's GLSL for uam, from the package in the NRO.
  if (IsDeko3D())
    return banjo::renut_gpu::deko3d::CreateGameShader(device, entry.hash);
#endif

  if (IsVulkan()) {
    std::vector<uint8_t> spirv;
    if (!EntrySpirv(entry, spirv))
      return nullptr;
    return device->createShader(spirv.data(), spirv.size(), "main",
                                plume::RenderShaderFormat::SPIRV);
  }

#if defined(_WIN32)
  const uint8_t *cache = Decompress(g_dxil, g_compressedDxilCache, g_dxilCacheCompressedSize,
                                    g_dxilCacheDecompressedSize, "DXIL");
  if (!cache || entry.dxilSize == 0)
    return nullptr;
  return device->createShader(cache + entry.dxilOffset, entry.dxilSize, "main",
                              plume::RenderShaderFormat::DXIL);
#else
  return nullptr;
#endif
}

std::string SpirvPackageKey() {
  SpirvPackage &p = OpenSpirvPackage();
  if (!p.ok)
    return {};
  return fmt::format("|package {} {:016X}", p.header->count, XXH3_64bits(p.file.data(), p.file.size()));
}

uint64_t ShaderCacheHostBytes() {
  return g_decompressed_bytes.load(std::memory_order_relaxed);
}

void CaptureMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash) {
  std::lock_guard lock(g_capture_mutex);
  if (!g_captured.insert(hash).second)
    return;

  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  // banjo-nx: next to the executable (on the Switch, the NRO's folder), not the working directory.
  const std::filesystem::path dir = banjo::renut_gpu::NextToExecutable(REXCVAR_GET(renut_shader_capture_dir));
  const std::filesystem::path path =
      dir / fmt::format("{}_{:016x}.bin", pixel_shader ? "ps" : "vs", hash);

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(path, std::ios::binary);
  if (out)
    out.write(reinterpret_cast<const char *>(container), std::streamsize(size));
  if (!out) {
    RGPU_WARN("Shader {:016X} is not in the shader cache and could not be captured to {}", hash,
              path.string());
    return;
  }
  RGPU_WARN("Shader {:016X} is not in the shader cache: captured to {}, the next build adds it",
            hash, path.string());
}

} // namespace renut::gpu
