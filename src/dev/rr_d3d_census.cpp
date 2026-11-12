// RR-NR-5 (Stop 1): observe-only D3D call census for Rumble Roses XX. Port of UFC-native
// src/dev/ufc_d3d_census.cpp (NR-6 / NR-6b) to RR's XDK 2.0.2500 (src/rr_d3d.h, rr_d3d_names.h).
//
// --rrxx_d3d_census=true counts calls to the game's XDK D3D functions per Swap and logs a [census]
// report every --rrxx_d3d_census_every frames. The emulated GPU still draws: every hook runs the
// game's own function unchanged, so with the cvar off (default) the exe behaves like the v1.5c one
// (one flag test per call). Built only while RRXX_NATIVE_HOOKS is off (cmake/native_renderer.cmake),
// because the native hooks override the same functions.
//
// What it answers (Stop 1, reports/PLAN-rr-nr2.md):
//   - draws per frame the hooks see: BeginVertices / DrawVertices / DrawIndexedVertices, plus the draw
//     packets inside the ring range each Clear and Resolve writes. Compare with the emulated GPU's own
//     count: --trace_draw_ps=0 --trace_draw_every=N logs "Draw trace c.f (frame F): end, X of Y draws
//     logged" (SDK patch my-fixes/v010-native-0028-trace-draw-ps.patch); tools/nr_census_report.py
//     (UFC) lines the two up. RR has no command buffers, ZPass, tiling, queries or
//     SetShaderGPRAllocation (FINDINGS-rr-nr1), so there is no replay count.
//   - device offsets (rr_d3d.h), each checked against the arguments of the setter that writes it:
//     VS +0x3294, PS +0x3290 (and the public VS slot +0x4FFC; RR-NR-6 fixed the VS/PS names), decl +0x2C90 at every draw; after each
//     setter: IB +0x30F4, RT +0x30F8 + 4i, DS +0x3108, stream {offset, vb} +0x310C + 8s and stride/4
//     +0x3190 + s, texture +0x3190 + 16 + 4s (= +0x31A0), viewport +0x3208 (24 bytes vs the argument),
//     scissor shadows +0x2D04/+0x2D08. The unknown dword +0x318C is logged (distinct values).
//   - the register shadow blocks: every (register, device offset) pair the draw flush's block writers
//     (sub_82076640 / sub_82076838 / sub_82076700, called from sub_82077A98) are handed, logged once each.
//   - the render-state call tables +0x88 [87] / +0x1E4 [20] (and the Get tables +0x234 / +0x390),
//     compared once with the default {get, set, default} triples at 0x82490700 / 0x82490B18.
//   - resolve targets (keep-1x list): every distinct (source RT/depth, dest texture size, format,
//     caller) a Resolve gets, logged once with a running count in each report.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <tuple>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "rr_d3d.h"
#include "rr_d3d_names.h"

REXCVAR_DEFINE_BOOL(rrxx_d3d_census, false, "rrxx",
                    "RR-NR-5: count the game's D3D calls per frame (draws, clears, resolves), check "
                    "the device offsets against the setters' arguments and log a [census] report "
                    "every rrxx_d3d_census_every frames. Observe only; the emulated GPU still draws");
REXCVAR_DEFINE_INT32(rrxx_d3d_census_every, 300, "rrxx",
                     "rrxx_d3d_census: frames (Swaps) per report")
    .range(1, 100000);

namespace rrxx_census {

namespace d = rrxx::d3d;

enum Fn : int {
  kSwap, kPresent, kResolve, kClear, kClearF, kSetRenderTarget, kSetDepthStencil, kSetViewport,
  kSetScissor, kBeginVertices, kEndVertices, kDrawVertices, kDrawIndexedVertices, kSetVertexShader,
  kSetPixelShader, kSetVertexDeclaration, kSetTexture, kSetStreamSource, kSetIndices, kSetVsConstF,
  kSetPsConstF, kVbUnlock, kIbUnlock, kCreateVs, kCreatePs, kCreateDecl, kFlush, kBlockRegs,
  kBlockConsts, kBlockFetch, kFnCount
};
constexpr const char* kFnName[kFnCount] = {
    "Swap", "Present", "Resolve", "Clear", "ClearF", "SetRenderTarget", "SetDepthStencil",
    "SetViewport", "SetScissor", "BeginVertices", "EndVertices", "DrawVertices",
    "DrawIndexedVertices", "SetVS", "SetPS", "SetDecl", "SetTexture", "SetStreamSource",
    "SetIndices", "SetVSConstF", "SetPSConstF", "VBUnlock", "IBUnlock", "CreateVS", "CreatePS",
    "CreateDecl", "Flush_82077A98", "Blk_82076640", "Blk_82076838", "Blk_82076700"};

enum Tally : int {
  kDirect,        // draw calls (BeginVertices, DrawVertices, DrawIndexedVertices)
  kClearDraws,    // draw packets in the ring range each Clear wrote
  kResolveDraws,  // draw packets in the ring range each Resolve wrote (its copy + any clear)
  kResolveClearDraws, // ... of those, in Resolves with clear flags
  kScanSkipped,   // Clear / Resolve calls whose ring range couldn't be scanned (wrapped / kicked off)
  kDrawsInScan,   // draw packets in the ring ranges the draw calls themselves wrote
  kVsMatch, kVsMismatch, kPsMatch, kPsMismatch, kPsPubMatch, kPsPubMismatch, kDeclMatch,
  kDeclMismatch, kIbMatch, kIbMismatch, kRtMatch, kRtMismatch, kDsMatch, kDsMismatch,
  kStreamMatch, kStreamMismatch, kTexMatch, kTexMismatch, kVpMatch, kVpMismatch,
  kDrawStreamMatch, kDrawStreamMismatch, kDrawTexMatch, kDrawTexMismatch,
  kTallyCount
};

inline std::atomic<uint64_t> g_calls[kFnCount];
inline std::atomic<uint64_t> g_tally[kTallyCount];
inline std::atomic<uint32_t> g_frame_draws{0};
inline std::atomic<uint32_t> g_last_vs{0}, g_last_ps{0}, g_last_decl{0}, g_last_ib{0};
inline std::atomic<uint32_t> g_last_stream_vb[16], g_last_stream_off[16];
inline std::atomic<uint32_t> g_last_tex[32];
inline std::atomic<bool> g_dumped_device{false};

struct Window {
  std::mutex lock;
  uint64_t frames = 0;
  uint32_t min = UINT32_MAX, max = 0, last = 0;
  uint64_t frame_index = 0;
};
inline Window g_window;

// Logged-once sets (block pairs, +0x318C values, resolve targets, mismatch samples).
inline std::mutex g_once_lock;
inline std::set<std::tuple<uint32_t, uint32_t, uint32_t>> g_blocks;   // (writer, reg, offset)
inline std::set<uint32_t> g_318c;
inline std::set<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> g_resolves;  // (flags&7|depth, w, h, fmt|lr)
inline uint32_t g_mismatch_logs = 0;

inline uint32_t Load32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return _byteswap_ulong(v);
}
inline uint8_t Load8(uint8_t* base, uint32_t addr) { return base[addr]; }
inline bool Plausible(uint32_t addr) { return addr >= 0x10000; }
inline bool On() { return REXCVAR_GET(rrxx_d3d_census); }
inline void Count(Fn f) { g_calls[f].fetch_add(1, std::memory_order_relaxed); }
inline void Add(Tally t, uint64_t n = 1) { g_tally[t].fetch_add(n, std::memory_order_relaxed); }
inline void Check(bool ok, Tally match) { Add(ok ? match : Tally(match + 1)); }

template <typename... Args>
inline void Mismatch(fmt::format_string<Args...> f, Args&&... args) {
  std::lock_guard<std::mutex> g(g_once_lock);
  if (g_mismatch_logs >= 48) return;
  ++g_mismatch_logs;
  REXLOG_INFO("[census] mismatch: {}", fmt::format(f, std::forward<Args>(args)...));
}

inline uint32_t PhysToVirtual(uint32_t addr) {
  return addr >= 0x80000000u ? addr : 0xA0000000u + (addr & 0x1FFFFFFFu);
}

struct Pm4Count {
  uint32_t draws = 0, other3 = 0;
};

// Walks one PM4 range for DRAW_INDX (0x22) / DRAW_INDX_2 (0x36), following INDIRECT_BUFFER(_PFD).
inline void ScanPm4Va(uint8_t* base, uint32_t va, uint32_t dwords, int depth, Pm4Count& out) {
  if (!va || dwords > 0x100000 || depth > 4) return;
  for (uint32_t i = 0; i < dwords;) {
    const uint32_t h = Load32(base, va + 4 * i);
    const uint32_t type = h >> 30;
    if (type == 0) {
      i += 2 + ((h >> 16) & 0x3FFF);
    } else if (type == 1) {
      i += 3;
    } else if (type == 2) {
      i += 1;
    } else {
      const uint32_t op = (h >> 8) & 0x7F, count = ((h >> 16) & 0x3FFF) + 1;
      if (op == 0x22 || op == 0x36) {
        ++out.draws;
      } else if ((op == 0x3F || op == 0x37) && i + 2 < dwords) {
        ScanPm4Va(base, PhysToVirtual(Load32(base, va + 4 * (i + 1))),
                  Load32(base, va + 4 * (i + 2)) & 0xFFFFF, depth + 1, out);
      } else {
        ++out.other3;
      }
      i += 1 + count;
    }
  }
}

// Draw packets in the ring range a call wrote: the ring pointer (+0x0) is the last dword written
// (stwu pre-increment), so new data starts at before + 4. A KickOff in the call resets it: skipped.
inline bool ScanRing(uint8_t* base, uint32_t before, uint32_t after, Pm4Count& c) {
  if (after <= before || after - before >= 0x40000) return false;
  ScanPm4Va(base, before + 4, (after - before) / 4, 0, c);
  return true;
}

// Fetch constant dword2 = size_2d: width-1 [12:0], height-1 [25:13]; dword1 format [5:0].
inline void TextureSize(uint8_t* base, uint32_t tex, uint32_t& w, uint32_t& h, uint32_t& fmt) {
  w = h = fmt = 0;
  if (!Plausible(tex)) return;
  const uint32_t d1 = Load32(base, tex + d::kTextureFetch + 4);
  const uint32_t d2 = Load32(base, tex + d::kTextureFetch + 8);
  w = (d2 & 0x1FFF) + 1;
  h = ((d2 >> 13) & 0x1FFF) + 1;
  fmt = d1 & 0x3F;
}

inline void DumpDeviceOnce(uint8_t* base, uint32_t dev) {
  bool expected = false;
  if (!g_dumped_device.compare_exchange_strong(expected, true)) return;
  // Code pointers anywhere in +0x38..+0x480 (UFC: the render-state table started at +0x40).
  uint32_t code = 0, first = 0, last = 0;
  for (uint32_t off = 0x38; off < 0x480; off += 4) {
    const uint32_t v = Load32(base, dev + off);
    if (v >= 0x82000000u && v < 0x83400000u) {
      if (!code) first = off;
      last = off;
      ++code;
    }
  }
  REXLOG_INFO("[census] device {:08X}: {} code pointers in +0x38..+0x47C, first +0x{:X}, last +0x{:X}",
              dev, code, first, last);
  // Tables vs the defaults {get, set, default} (3 dwords per state): which column each holds.
  struct T {
    const char* name;
    uint32_t off, n, table;
  };
  const T tables[] = {{"SetRenderState", d::kSetRenderStateCall, 87, 0x82490700u},
                      {"SetSamplerState", d::kSetSamplerStateCall, 20, 0x82490B18u},
                      {"GetRenderState", d::kGetRenderStateCall, 87, 0x82490700u},
                      {"GetSamplerState", d::kGetSamplerStateCall, 20, 0x82490B18u}};
  for (const T& t : tables) {
    uint32_t col[3] = {}, code_n = 0;
    for (uint32_t i = 0; i < t.n; ++i) {
      const uint32_t v = Load32(base, dev + t.off + 4 * i);
      if (v >= 0x82000000u && v < 0x83400000u) ++code_n;
      for (uint32_t k = 0; k < 3; ++k)
        if (v && v == Load32(base, t.table + 12 * i + 4 * k)) ++col[k];
    }
    REXLOG_INFO("[census] device +0x{:X} {}[{}]: {} code pointers; equal to default triple column "
                "0 (get) {} / 1 (set) {} / 2 (default) {}; first {:08X} {:08X} {:08X}",
                t.off, t.name, t.n, code_n, col[0], col[1], col[2], Load32(base, dev + t.off),
                Load32(base, dev + t.off + 4), Load32(base, dev + t.off + 8));
  }
  std::string head;
  for (uint32_t off = 0; off < 0x40; off += 4) head += fmt::format(" {:08X}", Load32(base, dev + off));
  REXLOG_INFO("[census] device {:08X}: +0x00..+0x3C{}", dev, head);
  REXLOG_INFO("[census] device {:08X}: VS +0x3294 {:08X}, PS +0x3290 {:08X}, VS public +0x4FFC {:08X}, "
              "decl +0x2C90 {:08X}, IB +0x30F4 {:08X}, RT0 +0x30F8 {:08X}, DS +0x3108 {:08X}, "
              "+0x318C {:08X}, front +0x3698 {:08X}, back +0x369C {:08X}, create flags +0x5070 {:08X}",
              dev, Load32(base, dev + d::kVertexShader), Load32(base, dev + d::kPixelShader),
              Load32(base, dev + d::kVertexShaderPublic), Load32(base, dev + d::kVertexDeclaration),
              Load32(base, dev + d::kIndexBuffer), Load32(base, dev + d::kRenderTarget0),
              Load32(base, dev + d::kDepthStencil), Load32(base, dev + 0x318C),
              Load32(base, dev + d::kFrontBuffer), Load32(base, dev + d::kBackBuffer),
              Load32(base, dev + d::kCreateFlags));
}

inline void Note318C(uint8_t* base, uint32_t dev) {
  const uint32_t v = Load32(base, dev + 0x318C);
  std::lock_guard<std::mutex> g(g_once_lock);
  if (g_318c.size() < 16 && g_318c.insert(v).second)
    REXLOG_INFO("[census] +0x318C new value {:08X} ({} distinct)", v, g_318c.size());
}

// Every draw call: count it and check the device's shader / declaration / stream / texture slots
// against the last setter arguments.
inline void OnDraw(uint8_t* base, uint32_t dev) {
  DumpDeviceOnce(base, dev);
  Add(kDirect);
  g_frame_draws.fetch_add(1, std::memory_order_relaxed);
  const uint32_t vs = Load32(base, dev + d::kVertexShader), ps = Load32(base, dev + d::kPixelShader),
                 vs_pub = Load32(base, dev + d::kVertexShaderPublic),
                 decl = Load32(base, dev + d::kVertexDeclaration);
  const uint32_t lvs = g_last_vs.load(std::memory_order_relaxed),
                 lps = g_last_ps.load(std::memory_order_relaxed),
                 ldecl = g_last_decl.load(std::memory_order_relaxed);
  Check(vs == lvs, kVsMatch);
  Check(ps == lps, kPsMatch);
  Check(vs_pub == lvs, kPsPubMatch);  // tally kept under its old name: VS public slot
  Check(decl == ldecl, kDeclMatch);
  if (vs != lvs) Mismatch("draw VS +0x3294 {:08X} last SetVS {:08X}", vs, lvs);
  if (ps != lps) Mismatch("draw PS +0x3290 {:08X} last SetPS {:08X}", ps, lps);
  if (decl != ldecl) Mismatch("draw decl +0x2C90 {:08X} last SetDecl {:08X}", decl, ldecl);
  for (uint32_t s = 0; s < 16; ++s) {
    const uint32_t vb = Load32(base, dev + d::kStreamSource + 8 * s + 4);
    const uint32_t off = Load32(base, dev + d::kStreamSource + 8 * s);
    if (!vb && !g_last_stream_vb[s].load(std::memory_order_relaxed)) continue;
    Check(vb == g_last_stream_vb[s].load(std::memory_order_relaxed) &&
              off == g_last_stream_off[s].load(std::memory_order_relaxed),
          kDrawStreamMatch);
  }
  for (uint32_t s = 0; s < 16; ++s) {
    const uint32_t t = Load32(base, dev + d::kTexture + 4 * s);
    if (!t && !g_last_tex[s].load(std::memory_order_relaxed)) continue;
    Check(t == g_last_tex[s].load(std::memory_order_relaxed), kDrawTexMatch);
  }
}

inline uint64_t Take(std::atomic<uint64_t>& a) { return a.exchange(0, std::memory_order_relaxed); }

inline void OnSwap(uint8_t* base, uint32_t dev) {
  if (Plausible(dev)) Note318C(base, dev);
  const uint32_t draws = g_frame_draws.exchange(0, std::memory_order_relaxed);
  Window& w = g_window;
  std::lock_guard<std::mutex> guard(w.lock);
  ++w.frame_index;
  ++w.frames;
  w.last = draws;
  if (draws < w.min) w.min = draws;
  if (draws > w.max) w.max = draws;
  if (w.frames < uint64_t(REXCVAR_GET(rrxx_d3d_census_every))) return;

  const double f = double(w.frames);
  uint64_t calls[kFnCount], t[kTallyCount];
  for (int i = 0; i < kFnCount; ++i) calls[i] = Take(g_calls[i]);
  for (int i = 0; i < kTallyCount; ++i) t[i] = Take(g_tally[i]);
  // gpu-like = what the GPU runs as draws: the draw calls + the clear quads (Clear, and Resolves
  // with clear flags). A Resolve's own copy packet is a copy on the GPU, not a draw.
  const uint64_t resolve_extra =
      t[kResolveDraws] > calls[kResolve] ? t[kResolveDraws] - calls[kResolve] : 0;
  const uint64_t gpu_like = t[kDirect] + t[kClearDraws];
  // Same line shape as UFC's census so tools/nr_census_report.py (UFC) reads it.
  REXLOG_INFO("[census] frame {} ({} frames): gpu-like {:.1f}/frame (min {} max {} last {}) = direct {:.1f} "
              "+ internal 0.0 + replayed 0.0 + clears {:.1f} + gpr flush 0.0; recorded 0.0; runs 0 (0.0/frame, 0 predicated draws, "
              "0 empty, 0 nested; 0.0 draws/frame in the buffers themselves); command-buffer share 0.00%",
              w.frame_index, w.frames, double(gpu_like) / f, w.min, w.max, w.last, double(t[kDirect]) / f,
              double(t[kClearDraws]) / f);
  REXLOG_INFO("[census] frame {}: resolves {:.1f}/frame, draw packets in them {:.1f}/frame (extra over one "
              "per resolve {:.1f}, in clearing resolves {:.1f}); gpu-like + extra {:.1f}/frame; ring scans skipped {}; "
              "draw packets the draw calls wrote {:.1f}/frame",
              w.frame_index, double(calls[kResolve]) / f, double(t[kResolveDraws]) / f,
              double(resolve_extra) / f, double(t[kResolveClearDraws]) / f,
              (double(gpu_like) + double(resolve_extra)) / f, t[kScanSkipped], double(t[kDrawsInScan]) / f);
  REXLOG_INFO("[census] frame {}: device check (at draws) VS +0x3294 {}/{} match, PS +0x3290 {}/{}, VS public "
              "+0x4FFC {}/{}, decl +0x2C90 {}/{}, streams +0x310C {}/{}, textures +0x31A0 {}/{}",
              w.frame_index, t[kVsMatch], t[kVsMatch] + t[kVsMismatch], t[kPsMatch], t[kPsMatch] + t[kPsMismatch],
              t[kPsPubMatch], t[kPsPubMatch] + t[kPsPubMismatch], t[kDeclMatch], t[kDeclMatch] + t[kDeclMismatch],
              t[kDrawStreamMatch], t[kDrawStreamMatch] + t[kDrawStreamMismatch], t[kDrawTexMatch],
              t[kDrawTexMatch] + t[kDrawTexMismatch]);
  REXLOG_INFO("[census] frame {}: setter check IB +0x30F4 {}/{}, RT +0x30F8 {}/{}, DS +0x3108 {}/{}, "
              "stream {{offset, vb}} +0x310C / stride +0x3190 {}/{}, texture +0x31A0 {}/{}, viewport +0x3208 {}/{}",
              w.frame_index, t[kIbMatch], t[kIbMatch] + t[kIbMismatch], t[kRtMatch], t[kRtMatch] + t[kRtMismatch],
              t[kDsMatch], t[kDsMatch] + t[kDsMismatch], t[kStreamMatch], t[kStreamMatch] + t[kStreamMismatch],
              t[kTexMatch], t[kTexMatch] + t[kTexMismatch], t[kVpMatch], t[kVpMatch] + t[kVpMismatch]);
  std::string line;
  for (int i = 0; i < kFnCount; ++i) {
    if (!calls[i]) continue;
    line += fmt::format(" {}={:.1f}", kFnName[i], double(calls[i]) / f);
  }
  REXLOG_INFO("[census] frame {}: calls/frame{}", w.frame_index, line);
  w.frames = 0;
  w.min = UINT32_MAX;
  w.max = 0;
}

inline void NoteBlock(uint32_t writer, uint32_t dev, uint32_t reg, uint32_t src) {
  const uint32_t off = src - dev;
  std::lock_guard<std::mutex> g(g_once_lock);
  if (g_blocks.size() < 64 && g_blocks.emplace(writer, reg, off).second)
    REXLOG_INFO("[census] block writer {:08X}: register 0x{:04X} from device +0x{:X}", writer, reg, off);
}

}  // namespace rrxx_census

namespace rc = rrxx_census;

#define RRXX_CENSUS_COUNT(fn, idx)                       \
  REX_EXTERN(__imp__##fn);                               \
  extern "C" REX_FUNC(fn) {                              \
    if (rc::On()) rc::Count(rc::idx);                    \
    __imp__##fn(ctx, base);                              \
  }

RRXX_CENSUS_COUNT(D3DDevice_Present, kPresent)
RRXX_CENSUS_COUNT(D3DDevice_ClearF, kClearF)
RRXX_CENSUS_COUNT(D3DDevice_EndVertices, kEndVertices)
RRXX_CENSUS_COUNT(D3DDevice_SetVertexShaderConstantF, kSetVsConstF)
RRXX_CENSUS_COUNT(D3DDevice_SetPixelShaderConstantF, kSetPsConstF)
RRXX_CENSUS_COUNT(D3DVertexBuffer_Unlock, kVbUnlock)
RRXX_CENSUS_COUNT(D3DIndexBuffer_Unlock, kIbUnlock)
RRXX_CENSUS_COUNT(D3DDevice_CreateVertexShader, kCreateVs)
RRXX_CENSUS_COUNT(D3DDevice_CreatePixelShader, kCreatePs)
RRXX_CENSUS_COUNT(D3DDevice_CreateVertexDeclaration, kCreateDecl)
RRXX_CENSUS_COUNT(sub_82077A98, kFlush)

// Draws (dev, ...): counted and checked before the call; the ring range the call writes is scanned
// too (one packet per call expected; DrawIndexedVertices splits counts over 65535).
#define RRXX_CENSUS_DRAW(fn, idx)                                       \
  REX_EXTERN(__imp__##fn);                                              \
  extern "C" REX_FUNC(fn) {                                             \
    if (!rc::On()) {                                                    \
      __imp__##fn(ctx, base);                                           \
      return;                                                           \
    }                                                                   \
    const uint32_t dev = ctx.r3.u32;                                    \
    rc::Count(rc::idx);                                                 \
    rc::OnDraw(base, dev);                                              \
    const uint32_t before = rc::Load32(base, dev + rc::d::kRing);       \
    __imp__##fn(ctx, base);                                             \
    rc::Pm4Count c;                                                     \
    if (rc::ScanRing(base, before, rc::Load32(base, dev + rc::d::kRing), c)) \
      rc::Add(rc::kDrawsInScan, c.draws);                               \
  }

RRXX_CENSUS_DRAW(D3DDevice_DrawVertices, kDrawVertices)
RRXX_CENSUS_DRAW(D3DDevice_DrawIndexedVertices, kDrawIndexedVertices)

// BeginVertices writes its DRAW_INDX header; the vertices follow until EndVertices. Not scanned.
REX_EXTERN(__imp__D3DDevice_BeginVertices);
extern "C" REX_FUNC(D3DDevice_BeginVertices) {
  if (rc::On()) {
    rc::Count(rc::kBeginVertices);
    rc::OnDraw(base, ctx.r3.u32);
    rc::Add(rc::kDrawsInScan);
  }
  __imp__D3DDevice_BeginVertices(ctx, base);
}

#define RRXX_CENSUS_SET(fn, idx, last)                                \
  REX_EXTERN(__imp__##fn);                                            \
  extern "C" REX_FUNC(fn) {                                           \
    if (rc::On()) {                                                   \
      rc::Count(rc::idx);                                             \
      rc::last.store(ctx.r4.u32, std::memory_order_relaxed);          \
    }                                                                 \
    __imp__##fn(ctx, base);                                           \
  }

RRXX_CENSUS_SET(D3DDevice_SetVertexShader, kSetVertexShader, g_last_vs)
RRXX_CENSUS_SET(D3DDevice_SetPixelShader, kSetPixelShader, g_last_ps)
RRXX_CENSUS_SET(D3DDevice_SetVertexDeclaration, kSetVertexDeclaration, g_last_decl)

// SetIndices(dev, ib).
REX_EXTERN(__imp__D3DDevice_SetIndices);
extern "C" REX_FUNC(D3DDevice_SetIndices) {
  const bool on = rc::On();
  const uint32_t dev = ctx.r3.u32, ib = ctx.r4.u32;
  __imp__D3DDevice_SetIndices(ctx, base);
  if (!on) return;
  rc::Count(rc::kSetIndices);
  rc::g_last_ib.store(ib, std::memory_order_relaxed);
  const uint32_t v = rc::Load32(base, dev + rc::d::kIndexBuffer);
  rc::Check(v == ib, rc::kIbMatch);
  if (v != ib) rc::Mismatch("SetIndices({:08X}) -> +0x30F4 {:08X}", ib, v);
}

// SetRenderTarget(dev, index, surface).
REX_EXTERN(__imp__D3DDevice_SetRenderTarget);
extern "C" REX_FUNC(D3DDevice_SetRenderTarget) {
  const bool on = rc::On();
  const uint32_t dev = ctx.r3.u32, idx = ctx.r4.u32, surf = ctx.r5.u32;
  __imp__D3DDevice_SetRenderTarget(ctx, base);
  if (!on) return;
  rc::Count(rc::kSetRenderTarget);
  if (idx < 4) {
    const uint32_t v = rc::Load32(base, dev + rc::d::kRenderTarget0 + 4 * idx);
    rc::Check(v == surf, rc::kRtMatch);
    if (v != surf) rc::Mismatch("SetRenderTarget({}, {:08X}) -> +0x{:X} {:08X}", idx, surf,
                                rc::d::kRenderTarget0 + 4 * idx, v);
  }
}

// SetDepthStencilSurface(dev, surface).
REX_EXTERN(__imp__D3DDevice_SetDepthStencilSurface);
extern "C" REX_FUNC(D3DDevice_SetDepthStencilSurface) {
  const bool on = rc::On();
  const uint32_t dev = ctx.r3.u32, surf = ctx.r4.u32;
  __imp__D3DDevice_SetDepthStencilSurface(ctx, base);
  if (!on) return;
  rc::Count(rc::kSetDepthStencil);
  const uint32_t v = rc::Load32(base, dev + rc::d::kDepthStencil);
  rc::Check(v == surf, rc::kDsMatch);
  if (v != surf) rc::Mismatch("SetDepthStencilSurface({:08X}) -> +0x3108 {:08X}", surf, v);
}

// SetStreamSource(dev, stream, vb, offset, stride).
REX_EXTERN(__imp__D3DDevice_SetStreamSource);
extern "C" REX_FUNC(D3DDevice_SetStreamSource) {
  const bool on = rc::On();
  const uint32_t dev = ctx.r3.u32, s = ctx.r4.u32, vb = ctx.r5.u32, off = ctx.r6.u32, stride = ctx.r7.u32;
  __imp__D3DDevice_SetStreamSource(ctx, base);
  if (!on) return;
  rc::Count(rc::kSetStreamSource);
  if (s < 16) {
    rc::g_last_stream_vb[s].store(vb, std::memory_order_relaxed);
    rc::g_last_stream_off[s].store(off, std::memory_order_relaxed);
    const uint32_t e_off = rc::Load32(base, dev + rc::d::kStreamSource + 8 * s);
    const uint32_t e_vb = rc::Load32(base, dev + rc::d::kStreamSource + 8 * s + 4);
    const uint32_t e_stride = rc::Load8(base, dev + rc::d::kStreamStrideDiv4 + s);
    const bool ok = e_off == off && e_vb == vb && e_stride == ((stride >> 2) & 0xFF);
    rc::Check(ok, rc::kStreamMatch);
    if (!ok) rc::Mismatch("SetStreamSource({}, vb {:08X}, off {}, stride {}) -> {{{}, {:08X}}} stride/4 {}", s, vb,
                          off, stride, e_off, e_vb, e_stride);
  }
  rc::Note318C(base, dev);
}

// SetTexture(dev, sampler, texture).
REX_EXTERN(__imp__D3DDevice_SetTexture);
extern "C" REX_FUNC(D3DDevice_SetTexture) {
  const bool on = rc::On();
  const uint32_t dev = ctx.r3.u32, s = ctx.r4.u32, tex = ctx.r5.u32;
  __imp__D3DDevice_SetTexture(ctx, base);
  if (!on) return;
  rc::Count(rc::kSetTexture);
  if (s < 32) rc::g_last_tex[s].store(tex, std::memory_order_relaxed);
  if (s < 26) {  // +0x31A0 + 4 * 26 = +0x3208 (viewport)
    const uint32_t v = rc::Load32(base, dev + rc::d::kTexture + 4 * s);
    rc::Check(v == tex, rc::kTexMatch);
    if (v != tex) rc::Mismatch("SetTexture({}, {:08X}) -> +0x{:X} {:08X}", s, tex, rc::d::kTexture + 4 * s, v);
  } else {
    rc::Mismatch("SetTexture sampler {} (past the 26 slots before the viewport)", s);
  }
}

// SetViewport(dev, pViewport): integer X, Y, W, H, float MinZ, MaxZ at +0x3208.
REX_EXTERN(__imp__D3DDevice_SetViewport);
extern "C" REX_FUNC(D3DDevice_SetViewport) {
  const bool on = rc::On();
  const uint32_t dev = ctx.r3.u32, vp = ctx.r4.u32;
  uint32_t arg[6] = {};
  if (on && rc::Plausible(vp))
    for (int i = 0; i < 6; ++i) arg[i] = rc::Load32(base, vp + 4 * i);
  __imp__D3DDevice_SetViewport(ctx, base);
  if (!on || !rc::Plausible(vp)) return;
  rc::Count(rc::kSetViewport);
  bool ok = true;
  uint32_t v[6];
  for (int i = 0; i < 6; ++i) {
    v[i] = rc::Load32(base, dev + rc::d::kViewport + 4 * i);
    ok = ok && v[i] == arg[i];
  }
  rc::Check(ok, rc::kVpMatch);
  if (!ok) rc::Mismatch("SetViewport({} {} {} {} {:08X} {:08X}) -> +0x3208 {} {} {} {} {:08X} {:08X}", arg[0], arg[1],
                        arg[2], arg[3], arg[4], arg[5], v[0], v[1], v[2], v[3], v[4], v[5]);
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 4)
    rc::Mismatch("(info) SetViewport {} {} {}x{}: scissor shadows +0x2D04 {:08X} +0x2D08 {:08X}, PA_CL_VPORT "
                 "+0x2D48.. {:08X} {:08X} {:08X} {:08X}",
                 arg[0], arg[1], arg[2], arg[3], rc::Load32(base, dev + 0x2D04), rc::Load32(base, dev + 0x2D08),
                 rc::Load32(base, dev + 0x2D48), rc::Load32(base, dev + 0x2D4C), rc::Load32(base, dev + 0x2D50),
                 rc::Load32(base, dev + 0x2D54));
}

// SetScissorRect(dev, pRect): logs the window scissor shadows it leaves (+0x2D04 / +0x2D08).
REX_EXTERN(__imp__D3DDevice_SetScissorRect);
extern "C" REX_FUNC(D3DDevice_SetScissorRect) {
  const bool on = rc::On();
  const uint32_t dev = ctx.r3.u32, r = ctx.r4.u32;
  __imp__D3DDevice_SetScissorRect(ctx, base);
  if (!on) return;
  rc::Count(rc::kSetScissor);
  static std::atomic<int> logged{0};
  if (rc::Plausible(r) && logged.fetch_add(1) < 4)
    rc::Mismatch("(info) SetScissorRect {} {} {} {} -> +0x3220 {} {} {} {}, +0x2D04 {:08X} +0x2D08 {:08X}",
                 int32_t(rc::Load32(base, r)), int32_t(rc::Load32(base, r + 4)), int32_t(rc::Load32(base, r + 8)),
                 int32_t(rc::Load32(base, r + 12)), int32_t(rc::Load32(base, dev + 0x3220)),
                 int32_t(rc::Load32(base, dev + 0x3224)), int32_t(rc::Load32(base, dev + 0x3228)),
                 int32_t(rc::Load32(base, dev + 0x322C)), rc::Load32(base, dev + 0x2D04),
                 rc::Load32(base, dev + 0x2D08));
}

REX_EXTERN(__imp__D3DDevice_Swap);
extern "C" REX_FUNC(D3DDevice_Swap) {
  const uint32_t dev = ctx.r3.u32;
  __imp__D3DDevice_Swap(ctx, base);
  if (rc::On()) {
    rc::Count(rc::kSwap);
    rc::OnSwap(base, dev);
  }
}

// Clear(dev, Count, pRects, Flags, Color, Z, Stencil): its draws are the packets in the ring range
// it writes (one rect-list draw per rect, through ClearF).
REX_EXTERN(__imp__D3DDevice_Clear);
extern "C" REX_FUNC(D3DDevice_Clear) {
  if (!rc::On()) {
    __imp__D3DDevice_Clear(ctx, base);
    return;
  }
  const uint32_t dev = ctx.r3.u32;
  const uint32_t before = rc::Load32(base, dev + rc::d::kRing);
  __imp__D3DDevice_Clear(ctx, base);
  rc::Count(rc::kClear);
  rc::Pm4Count c;
  if (rc::ScanRing(base, before, rc::Load32(base, dev + rc::d::kRing), c)) {
    rc::Add(rc::kClearDraws, c.draws);
    rc::g_frame_draws.fetch_add(c.draws, std::memory_order_relaxed);
  } else {
    rc::Add(rc::kScanSkipped);
    rc::Add(rc::kClearDraws);  // at least one rect
    rc::g_frame_draws.fetch_add(1, std::memory_order_relaxed);
  }
}

// Resolve(dev, flags, pSourceRect, pDestTexture, pDestPoint, level, slice, pClearColor, Z, stencil,
// params): scan its ring range; log each distinct target (keep-1x list).
REX_EXTERN(__imp__D3DDevice_Resolve);
extern "C" REX_FUNC(D3DDevice_Resolve) {
  if (!rc::On()) {
    __imp__D3DDevice_Resolve(ctx, base);
    return;
  }
  const uint32_t dev = ctx.r3.u32, flags = ctx.r4.u32, tex = ctx.r6.u32;
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  const uint32_t rt = (flags & 4) ? rc::Load32(base, dev + rc::d::kDepthStencil)
                                  : rc::Load32(base, dev + rc::d::kRenderTarget0 + 4 * (flags & 3));
  const uint32_t before = rc::Load32(base, dev + rc::d::kRing);
  __imp__D3DDevice_Resolve(ctx, base);
  rc::Count(rc::kResolve);
  rc::Pm4Count c;
  if (rc::ScanRing(base, before, rc::Load32(base, dev + rc::d::kRing), c)) {
    rc::Add(rc::kResolveDraws, c.draws);
    if (flags & 0x300) rc::Add(rc::kResolveClearDraws, c.draws);
  } else {
    rc::Add(rc::kScanSkipped);
  }
  uint32_t w, h, fmt;
  rc::TextureSize(base, tex, w, h, fmt);
  std::lock_guard<std::mutex> g(rc::g_once_lock);
  if (rc::g_resolves.size() < 96 && rc::g_resolves.emplace(flags & 0x3FF, (w << 16) | h, fmt, lr).second)
    REXLOG_INFO("[census] resolve target: flags {:08X} ({} {}) src surface {:08X} -> tex {:08X} {}x{} fmt {} "
                "level {} from lr {:08X} (frame {})",
                flags, (flags & 4) ? "depth" : "rt", flags & 3, rt, tex, w, h, fmt, ctx.r8.u32, lr,
                rc::g_window.frame_index);
}

// Block writers of the draw flush sub_82077A98: (dev, mask, first register, source in the device).
REX_EXTERN(__imp__sub_82076640);
extern "C" REX_FUNC(sub_82076640) {
  if (rc::On()) {
    rc::Count(rc::kBlockRegs);
    rc::NoteBlock(0x82076640u, ctx.r3.u32, ctx.r5.u32, ctx.r6.u32);
  }
  __imp__sub_82076640(ctx, base);
}
REX_EXTERN(__imp__sub_82076838);
extern "C" REX_FUNC(sub_82076838) {
  if (rc::On()) {
    rc::Count(rc::kBlockConsts);
    rc::NoteBlock(0x82076838u, ctx.r3.u32, ctx.r5.u32, ctx.r6.u32);
  }
  __imp__sub_82076838(ctx, base);
}
REX_EXTERN(__imp__sub_82076700);
extern "C" REX_FUNC(sub_82076700) {
  if (rc::On()) {
    rc::Count(rc::kBlockFetch);
    rc::NoteBlock(0x82076700u, ctx.r3.u32, ctx.r5.u32, ctx.r6.u32);
  }
  __imp__sub_82076700(ctx, base);
}
