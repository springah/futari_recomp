// Merge in-game settings back into a sectioned ini without disturbing the
// player's file. Three rules:
//
//  1. A known (section, key) line gets its value replaced in place; everything
//     to the left of the '=' -- key spelling, spacing -- survives verbatim.
//  2. Comments, blank lines and unknown lines survive verbatim.
//  3. Raw cvar lines that have no friendly line land in a delimited "managed"
//     block inserted at the TOP LEVEL (before the first section header), which
//     is where the SDK's LoadConfig reads cvars from. An existing managed
//     block is replaced wholesale.
//
// Pure std -- no SDK dependencies -- so the logic is testable standalone.

#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ini_merge {

inline constexpr std::string_view kBlockBegin =
    "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---";
inline constexpr std::string_view kBlockEnd = "# --- end overlay settings ---";

struct SectionUpdate {
  std::string section;  // display name, e.g. "Display"; matched case-insensitively
  std::string key;      // e.g. "resolution"; matched case-insensitively
  std::string value;    // replacement value text (written as-is)
};

namespace detail {

inline std::string LowerTrim(std::string_view value) {
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

// The lines without their CR, and without the UTF-8 BOMs at their start (a file saved by PowerShell 5.1 or some
// editors, twice over, or pasted together from such files), so a marker or key after a BOM still matches.
inline std::vector<std::string> SplitLines(std::string_view text) {
  const auto add = [](std::vector<std::string>& lines, std::string_view line) {
    while (line.substr(0, 3) == "\xEF\xBB\xBF")
      line.remove_prefix(3);
    lines.emplace_back(line);
  };
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      if (start < text.size())
        add(lines, text.substr(start));
      break;
    }
    std::string_view line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    add(lines, line);
    start = end + 1;
  }
  return lines;
}

// "[Display]" -> "display"; empty when the line is not a section header.
inline std::string SectionOf(std::string_view line) {
  std::string trimmed = LowerTrim(line);
  if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
    return trimmed.substr(1, trimmed.size() - 2);
  return {};
}

// The indexes of the managed block's begin and end marker lines; empty when the file has no complete block (only both
// markers in order count, so a damaged file never loses anything else).
inline std::optional<std::pair<size_t, size_t>> ManagedBlockRange(const std::vector<std::string>& lines) {
  const auto begin_it = std::find_if(lines.begin(), lines.end(), [](const std::string& l) {
    return LowerTrim(l) == LowerTrim(kBlockBegin);
  });
  if (begin_it == lines.end())
    return std::nullopt;
  const auto end_it = std::find_if(begin_it, lines.end(), [](const std::string& l) {
    return LowerTrim(l) == LowerTrim(kBlockEnd);
  });
  if (end_it == lines.end())
    return std::nullopt;
  return std::pair{static_cast<size_t>(begin_it - lines.begin()), static_cast<size_t>(end_it - lines.begin())};
}

}  // namespace detail

// original: current file content. updates: friendly lines to set. raw_block:
// newline-separated "name = value" lines for the managed block ("" = no block;
// any existing block is still removed). Returns the merged file content.
inline std::string Merge(std::string_view original,
                         std::vector<SectionUpdate> updates,
                         std::string_view raw_block) {
  using detail::LowerTrim;
  using detail::SectionOf;

  std::vector<std::string> lines = detail::SplitLines(original);

  // Drop an existing managed block -- only when both markers are found in
  // order, so a damaged file never loses anything else.
  if (const auto block = detail::ManagedBlockRange(lines)) {
    auto after = lines.erase(lines.begin() + block->first, lines.begin() + block->second + 1);
    // Also swallow the blank separator a previous merge wrote after the
    // block, so repeated saves don't accumulate blank lines.
    if (after != lines.end() && LowerTrim(*after).empty())
      lines.erase(after);
  }

  std::vector<bool> consumed(updates.size(), false);

  // Emit any not-yet-consumed updates belonging to `section` (used when a
  // section ends without having contained the key line).
  const auto flush_section = [&](std::vector<std::string>& out, const std::string& section) {
    if (section.empty())
      return;
    for (size_t i = 0; i < updates.size(); ++i) {
      if (!consumed[i] && LowerTrim(updates[i].section) == section) {
        out.push_back(updates[i].key + " = " + updates[i].value);
        consumed[i] = true;
      }
    }
  };

  std::vector<std::string> out;
  out.reserve(lines.size() + updates.size() + 8);
  std::string current_section;

  for (const std::string& line : lines) {
    const std::string header = SectionOf(line);
    if (!header.empty()) {
      // Keys missing from the section that just ended go at its end, before
      // the blank line (if any) that precedes the next header.
      if (!current_section.empty() && !out.empty() && out.back().empty()) {
        std::string blank = std::move(out.back());
        out.pop_back();
        flush_section(out, current_section);
        out.push_back(std::move(blank));
      } else {
        flush_section(out, current_section);
      }
      current_section = header;
      out.push_back(line);
      continue;
    }

    if (!current_section.empty()) {
      const std::string trimmed = LowerTrim(line);
      const size_t eq = line.find('=');
      if (!trimmed.empty() && trimmed.front() != '#' && trimmed.front() != ';' &&
          eq != std::string::npos) {
        const std::string key = LowerTrim(std::string_view(line).substr(0, eq));
        bool replaced = false;
        for (size_t i = 0; i < updates.size(); ++i) {
          if (!consumed[i] && LowerTrim(updates[i].section) == current_section &&
              LowerTrim(updates[i].key) == key) {
            out.push_back(line.substr(0, eq + 1) + " " + updates[i].value);
            consumed[i] = true;
            replaced = true;
            break;
          }
        }
        if (replaced)
          continue;
      }
    }
    out.push_back(line);
  }
  flush_section(out, current_section);

  // Updates for sections the file no longer has get their section appended.
  for (size_t i = 0; i < updates.size(); ++i) {
    if (consumed[i])
      continue;
    if (!out.empty() && !out.back().empty())
      out.emplace_back();
    out.push_back("[" + updates[i].section + "]");
    const std::string section = LowerTrim(updates[i].section);
    for (size_t j = i; j < updates.size(); ++j) {
      if (!consumed[j] && LowerTrim(updates[j].section) == section) {
        out.push_back(updates[j].key + " = " + updates[j].value);
        consumed[j] = true;
      }
    }
  }

  // Managed block: top level, immediately before the first section header.
  const std::string raw_trimmed = LowerTrim(raw_block);
  if (!raw_trimmed.empty()) {
    std::vector<std::string> block;
    block.emplace_back(kBlockBegin);
    for (std::string& l : detail::SplitLines(raw_block)) {
      if (!LowerTrim(l).empty())
        block.push_back(std::move(l));
    }
    block.emplace_back(kBlockEnd);
    block.emplace_back();  // blank line after the block

    auto first_header = std::find_if(out.begin(), out.end(), [](const std::string& l) {
      return !SectionOf(l).empty();
    });
    out.insert(first_header, block.begin(), block.end());
  }

  std::string result;
  for (const std::string& line : out) {
    result += line;
    result += '\n';
  }
  return result;
}

}  // namespace ini_merge
