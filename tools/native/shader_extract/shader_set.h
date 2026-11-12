// Rumble Roses XX's shader containers, pulled from the player's own disc files. Port of
// research/rr-nr1/extract_shaders.py (+ tools/cut-content/rrpac.py): the two give the same files
// (same names, same bytes), because the shader cache built from them is keyed by container hash.
//
// RR keeps its shaders as old XDK containers (0x102A0E0x). They are written out unchanged; the
// patched XenosRecomp (native/patches/xenosrecomp-0e.patch) upgrades them in memory and keys the
// cache on these original bytes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rrxx_shader_cache {

struct ShaderFile {
  std::string name;    // <sha1[:16]>.bin
  std::string data;    // the XDK shader container, as on disc
  std::string source;  // where it was first seen, e.g. _startup_usa.pac/0@1010a8
};

struct ShaderSet {
  std::vector<ShaderFile> files;  // sorted by name
  size_t containers = 0;
  size_t from_pac = 0;
  size_t from_xex = 0;
  size_t old_format = 0;  // 0x102A0E containers (all of RR's)
  size_t bpe_blobs = 0;   // BPE blobs unpacked on the way
  uint64_t bpe_bytes = 0;
  std::string set_hash;   // sha1 over name + data of every file, first 16 hex digits
};

// Finds default.xex and pac\_startup_usa.pac in game_root, game_root\assets or game_root\game\assets.
// Throws std::runtime_error.
ShaderSet ExtractShaderSet(const std::filesystem::path& game_root);

// Writes every file plus _sources.txt into dir (created if missing).
void WriteShaderSet(const ShaderSet& set, const std::filesystem::path& dir);

std::string ReadFileBytes(const std::filesystem::path& path);
void WriteFileBytes(const std::filesystem::path& path, std::string_view data);
std::string PathText(const std::filesystem::path& path);  // UTF-8, for messages

}  // namespace rrxx_shader_cache
