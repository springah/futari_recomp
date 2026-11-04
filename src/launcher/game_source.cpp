/**
 * @file        game_source.cpp
 * @brief       scripts/extract_iso.py in C++ (see the header). Offsets and limits are the script's.
 */
#include "launcher/game_source.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <system_error>

#include <rex/filesystem.h>

#include "launcher/sha1.h"

#ifdef _WIN32
#define FUTARI_FSEEK _fseeki64
#else
#define FUTARI_FSEEK fseeko
#endif

namespace futari::launcher {

namespace {

constexpr uint32_t kTitleId = 0x435A07D7;  // Mushihimesama Futari Ver. 1.5
constexpr uint32_t kContentTypeDlc = 0x2;
constexpr uint32_t kContentTypeInstalledDisc = 0x7000;
constexpr size_t kSectorSize = 2048;
constexpr char kXdvdfsMagic[] = "MICROSOFT*XBOX*MEDIA";  // 20 bytes
constexpr uint64_t kCandidateOffsets[] = {
    0x10000,     // standard XGD / most disc rips; also GoD installs
    0x2080000,   // XGD2 backup partition
    0x0FDA0000,  // XGD2 with video partition (the Mushi Futari RF rip)
    0x1FB20000,  // XGD3
};
constexpr int kFindDepth = 4;

std::string Utf8(const std::filesystem::path& p) { return rex::path_to_utf8(p); }

std::string Thousands(uint64_t v) {
  std::string s = std::to_string(v);
  for (int i = int(s.size()) - 3; i > 0; i -= 3) {
    s.insert(size_t(i), ",");
  }
  return s;
}

uint32_t BeU32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint32_t BeU24(const uint8_t* p) { return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]); }
uint32_t LeU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t LeU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

bool IsXContentMagic(const uint8_t* p) {
  return std::memcmp(p, "LIVE", 4) == 0 || std::memcmp(p, "CON ", 4) == 0 || std::memcmp(p, "PIRS", 4) == 0;
}

struct File {
  FILE* f = nullptr;
  explicit File(const std::filesystem::path& p, const char* mode = "rb") {
#ifdef _WIN32
    const std::wstring wmode(mode, mode + std::strlen(mode));
    f = _wfopen(p.c_str(), wmode.c_str());
#else
    f = std::fopen(p.c_str(), mode);
#endif
  }
  ~File() {
    if (f) {
      std::fclose(f);
    }
  }
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  explicit operator bool() const { return f != nullptr; }
  size_t ReadAt(uint64_t pos, void* buf, size_t n) {
    if (FUTARI_FSEEK(f, int64_t(pos), SEEK_SET) != 0) {
      return 0;
    }
    return std::fread(buf, 1, n, f);
  }
};

// {content_type, title_id} of an XContent package (LIVE/CON/PIRS), or false.
bool ReadXContentHeader(const std::filesystem::path& path, uint32_t& content_type, uint32_t& title_id) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    return false;
  }
  File f(path);
  if (!f) {
    return false;
  }
  uint8_t head[0x400];
  const size_t got = f.ReadAt(0, head, sizeof(head));
  if (got < 0x364 || !IsXContentMagic(head)) {
    return false;
  }
  content_type = BeU32(head + 0x344);
  title_id = BeU32(head + 0x360);
  return true;
}

// Anything the XDVDFS reader can read a byte range from: an ISO, or a console install presented flat.
class Disc {
 public:
  virtual ~Disc() = default;
  virtual size_t ReadAt(uint64_t pos, void* buf, size_t n) = 0;
};

class IsoDisc final : public Disc {
 public:
  explicit IsoDisc(const std::filesystem::path& p) : file_(p) {}
  bool ok() const { return bool(file_); }
  size_t ReadAt(uint64_t pos, void* buf, size_t n) override { return file_.ReadAt(pos, buf, n); }

 private:
  File file_;
};

// A console disc install ("Games on Demand" / SVOD container) as one flat image. Beside the header file sits
// <name>.data/ holding Data0000, Data0001, ... of 0x1000-byte blocks: one level-1 hash-table block, then groups of
// [one level-0 hash-table block + 0xCC data blocks]. A full file carries 0xCB groups. The level-0 tables hash each
// data block, the level-1 table hashes the level-0 tables plus the NEXT file's level-1 table, and the header's root
// hash covers the first level-1 table, so Verify() proves the whole copy good before extraction starts.
class GodDisc final : public Disc {
 public:
  static constexpr uint64_t kBlock = 0x1000;
  static constexpr uint64_t kDataPerL0 = 0xCC;
  static constexpr uint64_t kDataPerFile = 0xCB * 0xCC;

  bool Open(const std::filesystem::path& header_path, std::string& error, Progress& progress) {
    header_path_ = header_path;
    File f(header_path);
    uint8_t header[0x400];
    if (!f || f.ReadAt(0, header, sizeof(header)) < 0x379 + 0x24 || !IsXContentMagic(header)) {
      error = Utf8(header_path.filename()) + ": not an XContent package";
      return false;
    }
    const uint8_t* desc = header + 0x379;  // the SVOD device descriptor
    std::memcpy(root_hash_.data(), desc + 0x04, 20);
    const uint8_t flags = desc[0x18];
    data_block_count_ = BeU24(desc + 0x19);
    const uint32_t data_block_offset = BeU24(desc + 0x1C);
    if (flags || data_block_offset) {
      char line[160];
      std::snprintf(line, sizeof(line),
                    "  NOTE: unusual container layout (flags=0x%x, dataBlockOffset=0x%x) - untested, attempting anyway",
                    flags, data_block_offset);
      progress.Log(line);
    }
    data_dir_ = header_path.parent_path() / (header_path.filename().string() + ".data");
    std::error_code ec;
    if (!std::filesystem::is_directory(data_dir_, ec)) {
      error = Utf8(data_dir_.filename()) + ": data folder is missing beside " + Utf8(header_path.filename());
      return false;
    }
    const uint64_t n_files = (data_block_count_ + kDataPerFile - 1) / kDataPerFile;
    for (uint64_t i = 0; i < n_files; ++i) {
      char name[16];
      std::snprintf(name, sizeof(name), "Data%04llu", static_cast<unsigned long long>(i));
      data_files_.push_back(data_dir_ / name);
    }
    handles_.resize(data_files_.size());
    size_ = data_block_count_ * kBlock;
    return true;
  }

  uint64_t size() const { return size_; }
  size_t file_count() const { return data_files_.size(); }

  // (data blocks, expected byte size) of data file `index`.
  void FileShape(size_t index, uint64_t& blocks, uint64_t& bytes) const {
    blocks = std::min(kDataPerFile, data_block_count_ - index * kDataPerFile);
    const uint64_t groups = (blocks + kDataPerL0 - 1) / kDataPerL0;
    bytes = (1 + groups + blocks) * kBlock;
  }

  // Cheap structural check: every data file present and full size.
  std::vector<std::string> CheckFiles() const {
    std::vector<std::string> problems;
    for (size_t i = 0; i < data_files_.size(); ++i) {
      uint64_t blocks, expected;
      FileShape(i, blocks, expected);
      std::error_code ec;
      if (!std::filesystem::exists(data_files_[i], ec)) {
        problems.push_back(Utf8(data_files_[i].filename()) + " is missing (the install has " +
                           std::to_string(data_files_.size()) + " data files)");
      } else if (std::filesystem::file_size(data_files_[i], ec) != expected) {
        problems.push_back(Utf8(data_files_[i].filename()) + " is " +
                           Thousands(std::filesystem::file_size(data_files_[i], ec)) + " bytes, expected " +
                           Thousands(expected) + " - the copy is incomplete");
      }
    }
    return problems;
  }

  // Walks the container's own hash tree: root hash -> level-1 tables -> level-0 tables -> every data block.
  std::vector<std::string> Verify(Progress& progress) {
    std::vector<std::string> problems;
    Sha1::Digest expected_l1 = root_hash_;
    std::vector<uint8_t> l1(kBlock), l0(kBlock), block(kBlock);
    for (size_t i = 0; i < data_files_.size(); ++i) {
      if (progress.cancel.load()) {
        break;
      }
      uint64_t n_blocks, bytes;
      FileShape(i, n_blocks, bytes);
      const std::string name = Utf8(data_files_[i].filename());
      progress.Log("  verifying " + name + " ...");
      progress.SetFraction(float(i) / float(data_files_.size()));
      std::string bad;
      File f(data_files_[i]);
      uint64_t pos = 0;
      if (!f || f.ReadAt(pos, l1.data(), kBlock) != kBlock) {
        bad = name + ": cannot read its level-1 hash table";
      } else if (Sha1::Of(l1.data(), kBlock) != expected_l1) {
        bad = name + ": level-1 hash table does not match";
      } else {
        pos += kBlock;
        uint64_t done = 0, group = 0;
        while (done < n_blocks && bad.empty()) {
          const uint64_t take = std::min(kDataPerL0, n_blocks - done);
          if (f.ReadAt(pos, l0.data(), kBlock) != kBlock) {
            bad = name + ": short read at its hash table for blocks " + std::to_string(done);
            break;
          }
          pos += kBlock;
          if (std::memcmp(l1.data() + group * 20, Sha1::Of(l0.data(), kBlock).data(), 20) != 0) {
            bad = name + ": hash table for blocks " + std::to_string(done) + "-" + std::to_string(done + take - 1) +
                  " does not match";
            break;
          }
          for (uint64_t j = 0; j < take; ++j) {
            if (f.ReadAt(pos, block.data(), kBlock) != kBlock) {
              bad = name + ": short read at data block " + std::to_string(done + j);
              break;
            }
            pos += kBlock;
            if (std::memcmp(l0.data() + j * 20, Sha1::Of(block.data(), kBlock).data(), 20) != 0) {
              bad = name + ": data block " + std::to_string(done + j) + " fails its hash";
              break;
            }
          }
          done += take;
          ++group;
        }
      }
      if (!bad.empty()) {
        progress.Log("    CORRUPT");
        problems.push_back(bad + " - the copy is corrupt");
      } else {
        progress.Log("    ok");
      }
      // entry 0xCB of each level-1 table hashes the next file's level-1
      std::memcpy(expected_l1.data(), l1.data() + 0xCB * 20, 20);
    }
    return problems;
  }

  size_t ReadAt(uint64_t pos, void* buf, size_t n) override {
    size_t total = 0;
    auto* out = static_cast<uint8_t*>(buf);
    while (n > 0 && pos < size_) {
      const uint64_t block = pos / kBlock, within = pos % kBlock;
      const uint64_t index = block / kDataPerFile, fb = block % kDataPerFile;
      // skip the level-1 table and one level-0 table per 0xCC blocks
      const uint64_t offset = (fb + fb / kDataPerL0 + 2) * kBlock + within;
      const size_t take = size_t(std::min<uint64_t>(n, kBlock - within));
      File* f = Handle(size_t(index));
      if (!f) {
        break;
      }
      const size_t got = f->ReadAt(offset, out, take);
      if (got == 0) {
        break;
      }
      out += got;
      total += got;
      pos += got;
      n -= got;
    }
    return total;
  }

 private:
  File* Handle(size_t index) {
    if (index >= handles_.size()) {
      return nullptr;
    }
    if (!handles_[index]) {
      handles_[index] = std::make_unique<File>(data_files_[index]);
      if (!*handles_[index]) {
        handles_[index].reset();
        return nullptr;
      }
    }
    return handles_[index].get();
  }

  std::filesystem::path header_path_, data_dir_;
  Sha1::Digest root_hash_{};
  uint64_t data_block_count_ = 0;
  uint64_t size_ = 0;
  std::vector<std::filesystem::path> data_files_;
  std::vector<std::unique_ptr<File>> handles_;
};

// Walks `root` to kFindDepth levels, calling fn(file path, the names in its folder).
template <typename Fn>
void Walk(const std::filesystem::path& root, int depth, Fn&& fn) {
  if (depth >= kFindDepth) {
    return;
  }
  std::error_code ec;
  std::vector<std::filesystem::path> files, dirs;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (entry.is_directory(ec)) {
      dirs.push_back(entry.path());
    } else if (entry.is_regular_file(ec)) {
      files.push_back(entry.path());
    }
  }
  for (const auto& f : files) {
    fn(f, dirs);
  }
  for (const auto& d : dirs) {
    Walk(d, depth + 1, fn);
  }
}

// Disc-install containers (header file + <name>.data folder) for this game under `root`.
std::vector<std::filesystem::path> FindGodInstalls(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> found;
  Walk(root, 0, [&](const std::filesystem::path& file, const std::vector<std::filesystem::path>& dirs) {
    const std::filesystem::path data = file.parent_path() / (file.filename().string() + ".data");
    if (std::find(dirs.begin(), dirs.end(), data) == dirs.end()) {
      return;
    }
    uint32_t type, title;
    if (ReadXContentHeader(file, type, title) && type == kContentTypeInstalledDisc && title == kTitleId) {
      found.push_back(file);
    }
  });
  return found;
}

std::vector<std::filesystem::path> FindDlcPackages(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> found;
  Walk(root, 0, [&](const std::filesystem::path& file, const std::vector<std::filesystem::path>&) {
    uint32_t type, title;
    if (ReadXContentHeader(file, type, title) && type == kContentTypeDlc && title == kTitleId) {
      found.push_back(file);
    }
  });
  return found;
}

// --- XDVDFS (the disc filesystem, inside an ISO or a disc install) ---------------------------------------------

// The volume descriptor sits at sector 32 of its partition and every sector number in the filesystem is
// partition-relative, so the base for all reads is the descriptor's offset MINUS 0x10000.
bool FindVolumeDescriptor(Disc& disc, uint64_t& fs_base, uint32_t& root_sector, uint32_t& root_size,
                          Progress& progress) {
  uint8_t header[0x800];
  auto check = [&](uint64_t base) {
    if (disc.ReadAt(base, header, sizeof(header)) < sizeof(header)) {
      return false;
    }
    if (std::memcmp(header, kXdvdfsMagic, 20) != 0 || std::memcmp(header + 0x7EC, kXdvdfsMagic, 20) != 0) {
      return false;
    }
    char line[96];
    std::snprintf(line, sizeof(line), "  Found XDVDFS volume descriptor at offset 0x%08llX",
                  static_cast<unsigned long long>(base));
    progress.Log(line);
    root_sector = LeU32(header + 0x14);
    root_size = LeU32(header + 0x18);
    fs_base = base - 0x10000;
    return true;
  };
  for (uint64_t base : kCandidateOffsets) {
    if (check(base)) {
      return true;
    }
  }
  progress.Log("  Known offsets failed - scanning for the XDVDFS magic (may take a moment)...");
  std::vector<uint8_t> chunk(1 << 20);
  const uint64_t limit = 2ull << 30;
  uint64_t offset = 0;
  while (offset < limit) {
    if (progress.cancel.load()) {
      return false;
    }
    const size_t got = disc.ReadAt(offset, chunk.data(), chunk.size());
    if (got == 0) {
      break;
    }
    const uint8_t* begin = chunk.data();
    const uint8_t* end = begin + got;
    for (const uint8_t* p = begin; p + 20 <= end; ++p) {
      p = static_cast<const uint8_t*>(std::memchr(p, 'M', size_t(end - p)));
      if (!p) {
        break;
      }
      if (p + 20 <= end && std::memcmp(p, kXdvdfsMagic, 20) == 0 && check(offset + uint64_t(p - begin))) {
        return true;
      }
    }
    offset += got;
  }
  return false;
}

struct DirEntry {
  std::string name;
  uint32_t start_sector = 0;
  uint32_t size = 0;
  bool is_dir = false;
};

std::vector<DirEntry> ReadDirectory(Disc& disc, uint64_t fs_base, uint32_t sector, uint32_t size) {
  std::vector<uint8_t> data(size);
  const size_t got = disc.ReadAt(fs_base + uint64_t(sector) * kSectorSize, data.data(), size);
  data.resize(got);
  std::vector<DirEntry> entries;
  size_t offset = 0;
  while (offset + 14 <= data.size()) {
    const uint16_t left = LeU16(&data[offset]), right = LeU16(&data[offset + 2]);
    if (left == 0xFFFF && right == 0xFFFF) {
      offset += 4;  // empty sentinel: skip 4 bytes and try to align
      continue;
    }
    DirEntry e;
    e.start_sector = LeU32(&data[offset + 4]);
    e.size = LeU32(&data[offset + 8]);
    const uint8_t attributes = data[offset + 12];
    const uint8_t name_len = data[offset + 13];
    if (name_len == 0 || offset + 14 + name_len > data.size()) {
      break;
    }
    e.name.assign(reinterpret_cast<const char*>(&data[offset + 14]), name_len);
    e.is_dir = (attributes & 0x10) != 0;
    entries.push_back(std::move(e));
    offset += (size_t(14) + name_len + 3) & ~size_t(3);  // entries are 4-byte aligned
  }
  return entries;
}

// Rejects entry names that would write outside the output directory.
bool SafeName(const std::string& name) {
  return !(name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
           name.find('\\') != std::string::npos || name.find(':') != std::string::npos);
}

struct Extractor {
  Disc& disc;
  uint64_t fs_base;
  Progress& progress;
  std::vector<std::string>& problems;
  size_t files = 0;
  uint64_t total_bytes = 0, done_bytes = 0;

  uint64_t Measure(uint32_t sector, uint32_t size) {
    uint64_t bytes = 0;
    for (const auto& e : ReadDirectory(disc, fs_base, sector, size)) {
      bytes += e.is_dir ? Measure(e.start_sector, e.size) : e.size;
    }
    return bytes;
  }

  void Tree(uint32_t sector, uint32_t size, const std::filesystem::path& dest, const std::string& rel) {
    std::error_code ec;
    std::vector<uint8_t> buffer(1 << 16);
    for (const auto& e : ReadDirectory(disc, fs_base, sector, size)) {
      if (progress.cancel.load()) {
        return;
      }
      const std::string rel_path = rel.empty() ? e.name : rel + "/" + e.name;
      if (!SafeName(e.name)) {
        problems.push_back(rel_path + ": unsafe entry name in the directory table, skipped");
        progress.Log("  SKIPPED    " + rel_path + "  (unsafe name - corrupt directory table?)");
        continue;
      }
      const std::filesystem::path item = dest / e.name;
      if (e.is_dir) {
        std::filesystem::create_directories(item, ec);
        Tree(e.start_sector, e.size, item, rel_path);
        continue;
      }
      std::filesystem::create_directories(item.parent_path(), ec);
      uint64_t pos = fs_base + uint64_t(e.start_sector) * kSectorSize;
      uint64_t remaining = e.size, written = 0;
      {
        File out(item, "wb");
        if (!out) {
          problems.push_back(rel_path + ": cannot create the file");
          progress.Log("  FAILED     " + rel_path + "  (cannot create)");
          continue;
        }
        while (remaining > 0) {
          const size_t take = size_t(std::min<uint64_t>(buffer.size(), remaining));
          const size_t got = disc.ReadAt(pos, buffer.data(), take);
          if (got == 0) {
            break;
          }
          if (std::fwrite(buffer.data(), 1, got, out.f) != got) {
            break;
          }
          pos += got;
          written += got;
          remaining -= got;
          done_bytes += got;
        }
      }
      if (total_bytes > 0) {
        progress.SetFraction(float(double(done_bytes) / double(total_bytes)));
      }
      if (written != e.size) {
        problems.push_back(rel_path + ": wrote " + Thousands(written) + " of " + Thousands(e.size) +
                           " bytes (truncated)");
        progress.Log("  TRUNCATED  " + rel_path + "  (" + Thousands(written) + " of " + Thousands(e.size) + " bytes)");
      } else {
        ++files;
        progress.Log("  extracted  " + rel_path + "  (" + Thousands(written) + " bytes)");
      }
    }
  }
};

// DLC packages into dlc/, filenames unchanged (the game sniffs the XContent magic, so the console's hex names are
// exactly right).
size_t CopyDlc(const std::vector<std::filesystem::path>& packages, const std::filesystem::path& dlc_out,
               std::vector<std::string>& problems, Progress& progress) {
  std::error_code ec;
  std::filesystem::create_directories(dlc_out, ec);
  size_t installed = 0;
  for (const auto& src : packages) {
    const std::filesystem::path dest = dlc_out / src.filename();
    const auto src_size = std::filesystem::file_size(src, ec);
    if (std::filesystem::exists(dest, ec) && std::filesystem::file_size(dest, ec) == src_size) {
      progress.Log("  already present  " + Utf8(src.filename()) + "  (" + Thousands(src_size) + " bytes)");
      ++installed;
      continue;
    }
    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || std::filesystem::file_size(dest, ec) != src_size) {
      problems.push_back("dlc/" + Utf8(src.filename()) + ": copy came out short");
      progress.Log("  FAILED     " + Utf8(src.filename()));
    } else {
      ++installed;
      progress.Log("  installed  " + Utf8(src.filename()) + "  (" + Thousands(src_size) + " bytes)");
    }
  }
  return installed;
}

bool IsInside(const std::filesystem::path& inner, const std::filesystem::path& outer) {
  auto i = inner.begin(), o = outer.begin();
  for (; o != outer.end(); ++i, ++o) {
    if (i == inner.end() || *i != *o) {
      return false;
    }
  }
  return true;
}

size_t CopyTree(const std::filesystem::path& src, const std::filesystem::path& dst,
                const std::vector<std::filesystem::path>& skip, Progress& progress,
                std::vector<std::string>& problems) {
  std::error_code ec;
  size_t files = 0;
  std::filesystem::create_directories(dst, ec);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src, ec)) {
    if (progress.cancel.load()) {
      break;
    }
    const auto rel = std::filesystem::relative(entry.path(), src, ec);
    const auto target = dst / rel;
    if (entry.is_directory(ec)) {
      std::filesystem::create_directories(target, ec);
      continue;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    if (std::find(skip.begin(), skip.end(), entry.path()) != skip.end()) {
      continue;
    }
    std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      problems.push_back(Utf8(rel) + ": " + ec.message());
      ec.clear();
      continue;
    }
    ++files;
    if (files % 10 == 0 || rel.filename() == "default.xex") {
      progress.Log("  copied     " + Utf8(rel));
    }
  }
  // A folder that held only DLC packages (e.g. the dump's own dlc/) came across empty; drop it.
  for (const auto& p : skip) {
    const auto rel = std::filesystem::relative(p.parent_path(), src, ec);
    if (ec || rel.empty() || rel == ".") {
      ec.clear();
      continue;
    }
    const auto empty = dst / rel;
    if (empty != dst && std::filesystem::is_directory(empty, ec) && std::filesystem::is_empty(empty, ec)) {
      std::filesystem::remove(empty, ec);
    }
  }
  return files;
}

// An already-extracted game folder into assets/, leaving the DLC packages (they go to dlc/) alone.
size_t CopyLooseDump(const std::filesystem::path& src_in, const std::filesystem::path& out_in,
                     const std::vector<std::filesystem::path>& dlc, Progress& progress,
                     std::vector<std::string>& problems) {
  std::error_code ec;
  const auto src = std::filesystem::weakly_canonical(src_in, ec);
  const auto out = std::filesystem::weakly_canonical(out_in, ec);
  if (src == out) {
    progress.Log(Utf8(src) + " is already the assets folder; nothing to copy.");
    return 0;
  }
  if (IsInside(out, src)) {
    // The game files sit loose beside the exe (or above it): copying the folder into its own assets/ would drag
    // the port, its logs and a nested assets/ along. Only the game's own files, then.
    progress.Log(Utf8(src) + " contains the assets folder; copying only the game files (default.xex, media, ...)");
    std::filesystem::create_directories(out, ec);
    size_t files = 0;
    for (const char* name : {"default.xex", "media", "nxeart", "$SystemUpdate", "AvatarAssetPack"}) {
      const auto item = src / name;
      if (std::filesystem::is_directory(item, ec)) {
        files += CopyTree(item, out / name, {}, progress, problems);
      } else if (std::filesystem::is_regular_file(item, ec)) {
        std::filesystem::copy_file(item, out / name, std::filesystem::copy_options::overwrite_existing, ec);
        ++files;
      }
    }
    return files;
  }
  progress.Log("Copying your extracted game folder to " + Utf8(out) + "/");
  std::vector<std::filesystem::path> skip;
  for (const auto& p : dlc) {
    skip.push_back(std::filesystem::weakly_canonical(p, ec));
  }
  return CopyTree(src, out, skip, progress, problems);
}

}  // namespace

void Progress::Log(std::string line) {
  std::lock_guard<std::mutex> lock(mutex);
  lines.push_back(std::move(line));
}

void Progress::SetFraction(float f) {
  std::lock_guard<std::mutex> lock(mutex);
  fraction = f;
}

std::vector<std::string> Progress::Snapshot(float& fraction_out) {
  std::lock_guard<std::mutex> lock(mutex);
  fraction_out = fraction;
  return lines;
}

bool IsLooseDump(const std::filesystem::path& dir) {
  std::error_code ec;
  return std::filesystem::is_directory(dir, ec) && std::filesystem::is_regular_file(dir / "default.xex", ec) &&
         std::filesystem::is_directory(dir / "media", ec);
}

SourceInfo ClassifySource(const std::filesystem::path& path) {
  SourceInfo info;
  std::error_code ec;
  if (std::filesystem::is_regular_file(path, ec)) {
    uint32_t type, title;
    if (!ReadXContentHeader(path, type, title)) {
      info.kind = SourceKind::kIso;  // presumably an ISO
      info.iso = path;
      return info;
    }
    char hex[16];
    if (title != kTitleId) {
      std::snprintf(hex, sizeof(hex), "%08X", title);
      info.kind = SourceKind::kWrongTitle;
      info.note = Utf8(path.filename()) + " is an Xbox 360 package, but for title " + hex + ", not this game (" +
                  "435A07D7).";
      return info;
    }
    if (type == kContentTypeInstalledDisc) {
      info.kind = SourceKind::kConsoleInstall;
      info.god_header = path;
      return info;
    }
    if (type == kContentTypeDlc) {
      info.kind = SourceKind::kDlcOnly;
      info.dlc.push_back(path);
      return info;
    }
    std::snprintf(hex, sizeof(hex), "0x%x", type);
    info.kind = SourceKind::kUnsupported;
    info.note = Utf8(path.filename()) + ": unsupported package type " + hex + " (expected a disc install or DLC).";
    return info;
  }
  if (!std::filesystem::is_directory(path, ec)) {
    info.note = "Not found: " + Utf8(path);
    return info;
  }
  if (IsLooseDump(path)) {
    info.kind = SourceKind::kLooseFolder;
    info.loose = path;
    info.dlc = FindDlcPackages(path);
    return info;
  }
  const auto gods = FindGodInstalls(path);
  info.dlc = FindDlcPackages(path);
  if (!gods.empty()) {
    info.kind = SourceKind::kConsoleInstall;
    info.god_header = gods[0];
    if (gods.size() > 1) {
      info.note = std::to_string(gods.size()) + " disc installs found; using " + Utf8(gods[0]);
    }
    return info;
  }
  if (!info.dlc.empty()) {
    info.kind = SourceKind::kDlcOnly;
    return info;
  }
  info.note = "No game or DLC for this game found under " + Utf8(path) +
              ". Expected a disc .iso, the console's Content layout (a 00007000 folder, the installed disc, and/or "
              "a 00000002 folder, the DLC packages), or an already-extracted game folder with default.xex and "
              "media at its top.";
  return info;
}

ExtractResult ExtractGame(const std::filesystem::path& source, const std::filesystem::path& assets_dir,
                          const std::filesystem::path& dlc_dir, Progress& progress) {
  ExtractResult result;
  std::error_code ec;
  const SourceInfo info = ClassifySource(source);
  if (!info.note.empty()) {
    progress.Log(info.note);
  }
  switch (info.kind) {
    case SourceKind::kNone:
    case SourceKind::kWrongTitle:
    case SourceKind::kUnsupported:
      result.message = info.note;
      return result;
    default:
      break;
  }

  std::unique_ptr<Disc> disc;
  std::unique_ptr<GodDisc> god;
  if (info.kind == SourceKind::kLooseFolder) {
    result.files = CopyLooseDump(info.loose, assets_dir, info.dlc, progress, result.problems);
  } else if (info.kind == SourceKind::kConsoleInstall) {
    progress.Log("Opening console disc install: " + Utf8(info.god_header));
    god = std::make_unique<GodDisc>();
    std::string error;
    if (!god->Open(info.god_header, error, progress)) {
      result.message = error;
      return result;
    }
    auto structural = god->CheckFiles();
    if (structural.empty()) {
      progress.Log("Verifying the install against its hash tree (" + std::to_string(god->file_count()) +
                   " data files)...");
      structural = god->Verify(progress);
    }
    if (progress.cancel.load()) {
      result.status = ExtractResult::Status::kCancelled;
      result.message = "Cancelled.";
      return result;
    }
    if (!structural.empty()) {
      progress.Log("DISC INSTALL REJECTED - " + std::to_string(structural.size()) + " problem(s):");
      for (const auto& p : structural) {
        progress.Log("  " + p);
      }
      result.problems = structural;
      result.message = "Re-copy the install from your console's storage and try again; do not play from a copy "
                       "that fails these checks.";
      return result;
    }
  } else if (info.kind == SourceKind::kIso) {
    progress.Log("Opening ISO: " + Utf8(info.iso));
    auto iso = std::make_unique<IsoDisc>(info.iso);
    if (!iso->ok()) {
      result.message = "Cannot open " + Utf8(info.iso);
      return result;
    }
    disc = std::move(iso);
  }

  if (god || disc) {
    Disc& d = god ? static_cast<Disc&>(*god) : *disc;
    uint64_t fs_base;
    uint32_t root_sector, root_size;
    if (!FindVolumeDescriptor(d, fs_base, root_sector, root_size, progress)) {
      if (progress.cancel.load()) {
        result.status = ExtractResult::Status::kCancelled;
        result.message = "Cancelled.";
        return result;
      }
      result.message = "This is not an Xbox 360 disc image (no XDVDFS volume descriptor): the wrong file, or an ISO "
                       "that is encrypted, split, or in an unsupported format. If it is the right game, try "
                       "extracting with extract-xiso first (https://github.com/XboxDev/extract-xiso).";
      return result;
    }
    std::filesystem::create_directories(assets_dir, ec);
    progress.Log("  Root dir: sector " + std::to_string(root_sector) + ", size " + std::to_string(root_size) + " bytes");
    progress.Log("Extracting to " + Utf8(assets_dir) + "/");
    Extractor ex{d, fs_base, progress, result.problems};
    ex.total_bytes = ex.Measure(root_sector, root_size);
    ex.Tree(root_sector, root_size, assets_dir, "");
    result.files = ex.files;
  }

  if (progress.cancel.load()) {
    result.status = ExtractResult::Status::kCancelled;
    result.message = "Cancelled.";
    return result;
  }

  if (!info.dlc.empty()) {
    progress.Log("Installing " + std::to_string(info.dlc.size()) + " DLC package(s) to " + Utf8(dlc_dir) + "/");
    result.dlc_installed = CopyDlc(info.dlc, dlc_dir, result.problems, progress);
  }

  // Fail loudly on an incomplete extraction rather than reporting success.
  if (!result.problems.empty()) {
    progress.Log("EXTRACTION FAILED - " + std::to_string(result.problems.size()) + " problem(s):");
    for (const auto& p : result.problems) {
      progress.Log("  " + p);
    }
    result.message = "The dump is most likely truncated, split across files, or corrupt. Verify it against the "
                     "checksums in docs/DUMPING.md and re-dump it; do not play from what was extracted here.";
    return result;
  }

  if (info.kind == SourceKind::kDlcOnly) {
    if (std::filesystem::is_regular_file(assets_dir / "default.xex", ec)) {
      result.status = ExtractResult::Status::kOk;
      result.message = "DLC installed (the game data was already in place).";
      return result;
    }
    result.status = ExtractResult::Status::kDlcOnly;
    result.message = "Your DLC is installed, but that path holds no game: pick your disc .iso or your game folder next.";
    return result;
  }

  // default.xex at the assets root, where the runtime looks.
  std::filesystem::path xex = assets_dir / "default.xex";
  if (!std::filesystem::exists(xex, ec)) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_dir, ec)) {
      if (entry.is_regular_file(ec) && entry.path().filename() == "default.xex") {
        std::filesystem::copy_file(entry.path(), xex, std::filesystem::copy_options::overwrite_existing, ec);
        break;
      }
    }
  }
  if (!std::filesystem::is_regular_file(xex, ec)) {
    result.message = "default.xex was not found in the extracted files.";
    return result;
  }
  progress.Log("default.xex -> " + Utf8(xex));
  result.status = ExtractResult::Status::kOk;
  result.message = "Game data in place: " + std::to_string(result.files) + " files in " + Utf8(assets_dir) + "/" +
                   (result.dlc_installed ? ", " + std::to_string(result.dlc_installed) + " DLC package(s) in " +
                                               Utf8(dlc_dir) + "/"
                                         : std::string());
  return result;
}

}  // namespace futari::launcher
