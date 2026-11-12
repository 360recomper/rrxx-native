/**
 * @file    gpu/d3d.h
 * @brief   Byte-accurate guest layouts for the guest D3D9 SDK objects the
 *          recompiled BD engine reads/writes.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 *
 * Must match the guest layout byte-for-byte at the offsets the engine
 * accesses, or accessors like D3DTexture_GetLevelDesc decode garbage and
 * crash D3D12. Layouts recovered from the guest binary (D3DResource ord 933,
 * D3DTexture ord 2657, D3DSurface ord 2525, D3DVertexBuffer ord 1936,
 * D3DIndexBuffer ord 1937, D3DVertexShader ord 1641, D3DPixelShader ord 1263,
 * D3DVertexDeclaration ord 934, GPUTEXTURE_FETCH_CONSTANT ord 853). Only
 * host-accessed fields are named.
 *
 * RRXX_NATIVE_RENDERER (Rumble Roses XX, RR-NR-3): RR links XDK D3D 2.0.2500, older than UFC's and
 * SvR's. Its resources have a 12-byte header (refcount = low byte of Common, no ReferenceCount
 * word), 20-byte vertex / index buffers (fetch constant / address at +0x0C), 40-byte textures
 * (fetch constant at +0x10) and 64-byte surfaces; its device is 0x5080 bytes. Those branches sit
 * next to the UFC ones below; the RR function each offset comes from is in src/rr_d3d.h and
 * reports/FINDINGS-rr-nr1.md. Offsets are offline evidence only until the census (Stop 1).
 */
#pragma once

#include <cstddef>
#include <rex/types.h>

namespace bd::gpu {

#if defined(RRXX_NATIVE_RENDERER)
// RR (XDK 2.0.2500) D3DResource::Common:
//   bits  0-7   reference count (D3DResource_AddRef sub_8206D210 / Release sub_8206D790)
//   bits 16-18  resource type (below)
//   bits 19-23  device bind count: SetTexture / SetStreamSource add 0x80000; Release destroys only
//               when this and the refcount are 0
//   bit  24     set by every Create* (0x01xx0001)
//   bit  30     CreateVertexBuffer / CreateIndexBuffer / CreateTexture with a usage bit (0x41xx0001)
//   bit  31     index buffer: 32-bit indices; surface: set by CreateSurface on one path
enum class D3DResourceType : u32 {
  kVertexBuffer = 1,  // CreateVertexBuffer sub_8206D878: 0x01010001
  kIndexBuffer = 2,   // CreateIndexBuffer sub_8206D9A8: 0x01020001
  kTexture = 3,       // texture init sub_8206C698: 0x01030001
  kSurface = 4,       // CreateSurface sub_8206CCC8: 0x01040001
  // Cube / volume texture type values: not found yet (census).
};
constexpr u32 kCommonRefCountMask = 0x000000FFu;
constexpr u32 kCommonTypeShift = 16;
constexpr u32 kCommonTypeMask = 0x00070000u;
constexpr u32 kCommonBindCountMask = 0x00F80000u;
constexpr u32 kCommonBindCountOne = 0x00080000u;
constexpr u32 kCommonCreated = 0x01000000u;
constexpr u32 kCommonIndex32 = 0x80000000u;

// 12-byte header shared by every RR D3D resource.
struct D3DResource {
  be_u32 Common;    // +0x00  refcount (low byte), type, bind count, flags (above)
  be_u32 Fence;     // +0x04  SetStreamSource stores the device's last fence (+0x289C) on unbind
  be_u32 BaseFlush; // +0x08  0xFFFF0000 at create
};
static_assert(sizeof(D3DResource) == 12);
static_assert(offsetof(D3DResource, Fence) == 0x04);
static_assert(offsetof(D3DResource, BaseFlush) == 0x08);

inline u32 ResourceRefCount(const D3DResource &r) { return u32(r.Common) & kCommonRefCountMask; }
inline void SetResourceRefCount(D3DResource &r, u32 count) {
  r.Common = (u32(r.Common) & ~kCommonRefCountMask) | (count & kCommonRefCountMask);
}
inline u32 ResourceTypeBits(const D3DResource &r) {
  return (u32(r.Common) & kCommonTypeMask) >> kCommonTypeShift;
}
#else
// D3DRESOURCETYPE values from the guest D3D9 SDK. D3DResource::Common carries
// the type in its low 4 bits, so these double as the Common base type bits.
enum class D3DResourceType : u32 {
  kSurface = 1,
  kTexture = 3,
  kVolumeTexture = 4,
  kCubeTexture = 5,
  kVertexBuffer = 6,
  kIndexBuffer = 7,
};

// 24-byte header shared by every D3D9 resource (D3DResource ord 933).
struct D3DResource {
  be_u32 Common;         // +0x00  type/flag bits, read by D3DResource_GetType
  be_u32 ReferenceCount; // +0x04  engine increments/decrements directly
  be_u32 Fence;          // +0x08
  be_u32 ReadFence;      // +0x0C
  be_u32 Identifier;     // +0x10
  be_u32 BaseFlush;      // +0x14
};
static_assert(sizeof(D3DResource) == 24);
#endif  // RRXX_NATIVE_RENDERER

// The pViewport argument to D3DDevice_SetViewport (BD 0x82473610), and the
// layout of the device's cached viewport at D3DDevice+0x3058.
struct D3DViewport9 {
  be_u32 X;
  be_u32 Y;
  be_u32 Width;
  be_u32 Height;
  be_f32 MinZ;
  be_f32 MaxZ;
};
static_assert(sizeof(D3DViewport9) == 24);

// Guest RECT (LTRB): the pRect argument to D3DDevice_SetScissorRect
// (BD 0x82473548).
struct D3DRect {
  be_i32 left;
  be_i32 top;
  be_i32 right;
  be_i32 bottom;
};
static_assert(sizeof(D3DRect) == 16);

// GPUTEXTURE_FETCH_CONSTANT (ord 853): 6-dword Xenos texture descriptor at
// D3DTexture+0x1C. reblue-created textures leave it zero since their accessors
// are hooked. Engine-created bdAllocRenderBuffer textures carry a real one that
// native_texture_mirror decodes.
struct GpuTextureFetchConstant {
  be_u32 dword[6];
};
static_assert(sizeof(GpuTextureFetchConstant) == 24);

#if defined(RRXX_NATIVE_RENDERER)
// RR D3DTexture, 40 bytes (CreateTexture sub_8206CBA8 allocates 40; init sub_8206C698). SetTexture
// (sub_8206D0B8) copies the fetch constant from +0x10..+0x24 into device fetch slot 48 + sampler.
struct D3DTexture {
  D3DResource resource;           // +0x00
  be_u32 MipFlush;                // +0x0C  0xFFFF0000 at create
  GpuTextureFetchConstant Format; // +0x10  dword1 (+0x14) base address, dword5 (+0x24) mip address
};
static_assert(sizeof(D3DTexture) == 40);
static_assert(offsetof(D3DTexture, MipFlush) == 0x0C);
static_assert(offsetof(D3DTexture, Format) == 0x10);
#else
// D3DTexture / D3DBaseTexture / D3DCubeTexture / D3DVolumeTexture, 52 bytes.
struct D3DTexture {
  D3DResource resource;           // +0x00
  be_u32 MipFlush;                // +0x18
  GpuTextureFetchConstant Format; // +0x1C
};
static_assert(sizeof(D3DTexture) == 52);
static_assert(offsetof(D3DTexture, Format) == 0x1C);
#endif  // RRXX_NATIVE_RENDERER

#if defined(RRXX_NATIVE_RENDERER)
// RR D3DSurface, 64 bytes (CreateSurface sub_8206CCC8 allocates 64 and writes +0x10..+0x38). A
// surface of a texture level has refcount 0 and forwards AddRef / Release to Parent (+0x2C).
// Like a texture it carries a GPU fetch constant at +0x10 (RR-NR-6): GetDesc (sub_8206CFD0 ->
// sub_8206BE90) takes the size from dword2 (+0x18, sub_8206B040: 2D width-1 [12:0], height-1
// [25:13]) and packs the D3DFORMAT from dwords 0/1/3 (sub_8206B220, RrSurfaceFormat below).
// Common bit 25 (0x02000000) = a level of Parent (+0x2C), level in dword4 (+0x20) bits [5:2].
struct D3DSurface {
  D3DResource resource;           // +0x00
  be_u32 word0C;                  // +0x0C
  GpuTextureFetchConstant Fetch;  // +0x10..+0x27
  be_u32 word28;                  // +0x28
  be_u32 Parent;                  // +0x2C  owning texture (AddRef / Release when type == kSurface)
  be_u32 word30[4];               // +0x30..+0x3C (EDRAM base / pitch, written by CreateSurface)
};
static_assert(sizeof(D3DSurface) == 64);
static_assert(offsetof(D3DSurface, Fetch) == 0x10);
static_assert(offsetof(D3DSurface, Parent) == 0x2C);

namespace rr_detail {
constexpr u32 Rotl(u32 v, u32 n) { return n ? (v << n) | (v >> (32 - n)) : v; }
// PowerPC MASK(mb, me): bits mb..me set, bit 0 = MSB.
constexpr u32 Mask(u32 mb, u32 me) {
  u32 m = 0;
  for (u32 b = 0; b < 32; ++b) {
    const bool in = mb <= me ? (b >= mb && b <= me) : (b >= mb || b <= me);
    if (in) m |= 0x80000000u >> b;
  }
  return m;
}
constexpr u32 Rlwinm(u32 v, u32 sh, u32 mb, u32 me) { return Rotl(v, sh) & Mask(mb, me); }
constexpr u32 Rlwimi(u32 dst, u32 v, u32 sh, u32 mb, u32 me) {
  return (Rotl(v, sh) & Mask(mb, me)) | (dst & ~Mask(mb, me));
}
}  // namespace rr_detail

// D3DFORMAT of a surface or texture level from its fetch constant: sub_8206B220, instruction by
// instruction (d0 = +0x10, d1 = +0x14, d3 = +0x1C of the object).
inline u32 RrFetchD3DFormat(u32 d0, u32 d1, u32 d3) {
  using namespace rr_detail;
  u32 r11 = d3, r10 = d0, r3 = d1;
  u32 r8 = Rlwinm(r11, 25, 29, 31);
  u32 r9 = Rlwinm(r11, 25, 26, 28);
  u32 r7 = Rlwinm(r11, 28, 29, 31);
  r9 |= r8;
  r8 = Rlwinm(r11, 31, 29, 31);
  r9 = Rlwinm(r9, 3, 0, 28);
  u32 r6 = Rlwinm(r10, 26, 30, 31);
  r9 |= r7;
  r7 = Rlwinm(r10, 24, 30, 31);
  r9 = Rlwinm(r9, 3, 0, 28);
  r9 |= r8;
  r8 = Rlwinm(r10, 28, 30, 31);
  r11 = Rlwimi(r11, r9, 1, 0, 30);
  r9 = Rlwinm(r10, 30, 30, 31);
  r11 = Rlwinm(r11, 2, 0, 29);
  r10 = Rlwinm(r10, 1, 31, 31);
  r11 |= r7;
  r7 = Rlwinm(r3, 26, 30, 31);
  r11 = Rlwinm(r11, 2, 0, 29);
  r11 |= r6;
  r11 = Rlwinm(r11, 2, 0, 29);
  r11 |= r8;
  r11 = Rlwinm(r11, 2, 0, 29);
  r11 |= r9;
  r11 = Rlwinm(r11, 1, 0, 30);
  r11 |= r10;
  r11 = Rlwinm(r11, 2, 0, 29);
  r11 |= r7;
  return Rlwimi(r3, r11, 6, 0, 25);
}
inline u32 RrSurfaceFormat(const D3DSurface &s) {
  return RrFetchD3DFormat(u32(s.Fetch.dword[0]), u32(s.Fetch.dword[1]), u32(s.Fetch.dword[3]));
}
// 2D size from fetch dword2 (sub_8206B040's 2D path; tiled-pitch rounding not applied).
inline void RrSurfaceSize(const D3DSurface &s, u32 &width, u32 &height) {
  const u32 d2 = s.Fetch.dword[2];
  width = (d2 & 0x1FFFu) + 1u;
  height = ((d2 >> 13) & 0x1FFFu) + 1u;
}
#else
// D3DSurface, 48 bytes. D3DSurface_GetDesc (0x8246D668, non-texture path)
// unpacks Width/Height from the dword at +0x24:
//   Width  = (SizeBits >> 18) + 1            (width-1 in bits [31:18])
//   Height = ((SizeBits >> 3) & 0x7FFF) + 1  (height-1 in bits [17:3])
// Bits [2:0] are pad.
struct D3DSurface {
  D3DResource resource; // +0x00
  be_u32 SurfaceInfo;   // +0x18  GPU_SURFACEINFO union word
  be_u32 DepthInfo;     // +0x1C  GPU_DEPTHINFO union word
  be_u32 HiControl;     // +0x20
  be_u32 SizeBits;      // +0x24  packed Height/Width
  be_u32 Format;        // +0x28  D3DFORMAT
  be_u32 Size;          // +0x2C
};
static_assert(sizeof(D3DSurface) == 48);
#endif  // RRXX_NATIVE_RENDERER

// Verified against the code at 0x8246D668. Written by D3DSurface_GetDesc
// and D3DTexture_GetLevelDesc.
struct D3DSurfaceDesc {
  be_u32 Format;             // +0x00  guest D3DFORMAT
  be_u32 Type;               // +0x04  D3DRESOURCETYPE
  be_u32 Usage;              // +0x08  always written 0 by reblue
  be_u32 Pool;               // +0x0C  always written 0 by reblue
  be_u32 MultiSampleType;    // +0x10  0, since reblue surfaces are COUNT_1
  be_u32 MultiSampleQuality; // +0x14  0
  be_u32 Width;              // +0x18  surface / mip-level width
  be_u32 Height;             // +0x1C  surface / mip-level height
};
static_assert(sizeof(D3DSurfaceDesc) == 32);

// Written by D3DVolumeTexture_GetLevelDesc. Adds Depth and drops the
// MultiSample fields, so NOT the D3DSurfaceDesc layout.
struct D3DVolumeDesc {
  be_u32 Format; // +0x00  guest D3DFORMAT
  be_u32 Type;   // +0x04  4 == D3DRTYPE_VOLUMETEXTURE
  be_u32 Usage;  // +0x08  0
  be_u32 Pool;   // +0x0C  0
  be_u32 Width;  // +0x10  mip-level width
  be_u32 Height; // +0x14  mip-level height
  be_u32 Depth;  // +0x18  mip-level depth
};
static_assert(sizeof(D3DVolumeDesc) == 28);

// D3DLOCKED_RECT. Points the engine at the texture's guest scratch mirror.
struct D3DLockedRect {
  be_u32 Pitch; // +0x00  row pitch in bytes
  be_u32 pBits; // +0x04  guest VA of the locked pixel scratch
};
static_assert(sizeof(D3DLockedRect) == 8);

#if defined(RRXX_NATIVE_RENDERER)
// RR D3DVertexBuffer / D3DIndexBuffer, 20 bytes each.
// Vertex buffer (CreateVertexBuffer sub_8206D878): the Xenos vertex fetch constant follows the
// header, FetchLo = physical address | 3, FetchHi = (size & 0x3FFFFFC) | 2 (+ the endian bit kept
// from before). Unlock (sub_8206D998) reads the address as FetchLo & ~3.
// Index buffer (CreateIndexBuffer sub_8206D9A8): Address (+0x0C) and Size in bytes (+0x10);
// kCommonIndex32 in Common marks 32-bit indices.
struct D3DVertexBuffer {
  D3DResource resource;
  be_u32 FetchLo; // +0x0C  address | 3
  be_u32 FetchHi; // +0x10  size | 2
};
static_assert(sizeof(D3DVertexBuffer) == 20);
static_assert(offsetof(D3DVertexBuffer, FetchLo) == 0x0C);
static_assert(offsetof(D3DVertexBuffer, FetchHi) == 0x10);

// Named FetchLo / FetchHi like UFC's (SvR stock XDK: Address / Size), so the ported readers work.
// DrawIndexedVertices (sub_822E06E0) reads Common bit 31 (INDEX32) and Address (+0x0C); the
// endian swap is fixed by the index size (16-bit 8in16, 32-bit 8in32), not Common bits 29-30
// (bit 30 is RR's usage bit).
struct D3DIndexBuffer {
  D3DResource resource;
  be_u32 FetchLo; // +0x0C  Address: physical address
  be_u32 FetchHi; // +0x10  Size: bytes
};
static_assert(sizeof(D3DIndexBuffer) == 20);
static_assert(offsetof(D3DIndexBuffer, FetchLo) == 0x0C);
static_assert(offsetof(D3DIndexBuffer, FetchHi) == 0x10);

inline u32 VertexBufferAddress(const D3DVertexBuffer &vb) { return u32(vb.FetchLo) & ~3u; }

// Shader / declaration objects: the 12-byte header. RR's CreateVertexShader (sub_8207BD38) builds
// a 592-byte object and CreatePixelShader (sub_8207BC48) a 52-byte one; CreateVertexDeclaration
// (sub_8207BA98) 40 + 12 * elements. Fields past the header: not decoded yet.
struct D3DVertexShader {
  D3DResource resource;
};
struct D3DPixelShader {
  D3DResource resource;
};
struct D3DVertexDeclaration {
  D3DResource resource;
};
static_assert(sizeof(D3DVertexShader) == 12);
static_assert(sizeof(D3DPixelShader) == 12);
static_assert(sizeof(D3DVertexDeclaration) == 12);
#else
// D3DVertexBuffer / D3DIndexBuffer, 32 bytes. The +0x18/+0x1C pair is the Xenos
// fetch constant, encoded two ways depending on who wrote it:
//   Pre-XGOffset (bdPhysical*Create replication, raw dwords):
//     VB writes size@FetchLo/base@FetchHi, IB writes (size|3)@FetchLo/
//     ((base & 0x3FFFFFC)|0x10000002)@FetchHi.
//   Post-XGOffsetResourceAddress (asset-loaded structs, Bootstrap path):
//     FetchLo = base_va | tag_bits, FetchHi = (size & 0x3FFFFFC) | 0x10000002.
// reblue's own mirror and bootstrap paths use the post-XGOffset encoding.
struct D3DVertexBuffer {
  D3DResource resource;
  be_u32 FetchLo; // +0x18
  be_u32 FetchHi; // +0x1C
};
static_assert(sizeof(D3DVertexBuffer) == 32);
static_assert(offsetof(D3DVertexBuffer, FetchLo) == 0x18);
static_assert(offsetof(D3DVertexBuffer, FetchHi) == 0x1C);

struct D3DIndexBuffer {
  D3DResource resource;
  be_u32 FetchLo; // +0x18  SvR (stock XDK): Address, a CPU address with no tag bits
  be_u32 FetchHi; // +0x1C  SvR (stock XDK): Size, exact bytes
};
static_assert(sizeof(D3DIndexBuffer) == 32);
static_assert(offsetof(D3DIndexBuffer, FetchLo) == 0x18);
static_assert(offsetof(D3DIndexBuffer, FetchHi) == 0x1C);

// D3DVertexShader / D3DPixelShader / D3DVertexDeclaration, 24 bytes each.
struct D3DVertexShader {
  D3DResource resource;
};
struct D3DPixelShader {
  D3DResource resource;
};
struct D3DVertexDeclaration {
  D3DResource resource;
};
static_assert(sizeof(D3DVertexShader) == 24);
static_assert(sizeof(D3DPixelShader) == 24);
static_assert(sizeof(D3DVertexDeclaration) == 24);
#endif  // RRXX_NATIVE_RENDERER

// The microcode blob CreateVertexShader/CreatePixelShader are handed, layout
// mirrors thirdparty/XenosRecomp/XenosRecomp/shader.h.
// RR hands the old 0x102A0E layout (24-byte header: flags virt phys defTab ctab shader). Only
// flags, virtualSize, physicalSize and constantTableOffset (+0x10 in both layouts) may be read
// from an RR container through this struct; the cache key is XXH3 of virtualSize + physicalSize
// bytes (native/patches/xenosrecomp-0e.patch).
struct ShaderContainer {
  be_u32 flags;
  be_u32 virtualSize;
  be_u32 physicalSize;
  be_u32 fieldC;
  be_u32 constantTableOffset;
  be_u32 definitionTableOffset;
  be_u32 shaderOffset;
  be_u32 field1C;
  be_u32 field20;
};
static_assert(sizeof(ShaderContainer) == 0x24);

// Direct3D_CreateDevice allocates 0x5000 via
// D3D_AllocAlignedZeroed: the 0x2A00-byte public D3DDevice plus BD-private
// slots past it.
#if defined(RRXX_NATIVE_RENDERER)
constexpr u32 kD3DDeviceAllocSize = 0x5080;  // RR: Direct3D_CreateDevice sub_820706F0, li r3,20608
#else
constexpr u32 kD3DDeviceAllocSize = 0x5000;
#endif

// dword[0] bits [1:0] of a fetch constant tag what it describes.
constexpr u32 kFetchTypeMask = 0x3;
constexpr u32 kFetchTypeTexture = 2;
constexpr u32 kFetchTypeVertex = 3;

// 24-byte Xenos GPU fetch descriptor. D3DDevice holds 32 in fetchConstants[].
struct DeviceFetchConstant {
  be_u32 dword[6];
};
static_assert(sizeof(DeviceFetchConstant) == 24);

// Shared guest memory: the recompiled engine, reblue's hooks and the draw path
// all read and write it. Only the fields reblue reads are named. Verified
// against the recovered D3DDevice / _D3DConstants types and BD's own code. Each
// named field past the public struct cites where it came from.
#if defined(RRXX_NATIVE_RENDERER)
// Rumble Roses XX D3DDevice: 0x5080 bytes (Direct3D_CreateDevice sub_820706F0, li r3,20608). Its own
// layout, close to neither UFC (0x5F80) nor SvR 2007 (0x5000). Constants and fetch constants sit
// where UFC has them; render targets, streams, textures and shaders sit 0x160-0x1A0 past SvR 2007's.
// Source of each offset: reports/FINDINGS-rr-nr1.md (a), src/rr_d3d.h. Medium entries are marked;
// all of it is offline evidence until the census build (Stop 1) checks it at run time.
struct RrStreamSource {
  be_u32 offset;        // +0  SetStreamSource r6
  be_u32 vertexBuffer;  // +4  D3DVertexBuffer, refcounted through the Common bind count
};
static_assert(sizeof(RrStreamSource) == 8);

struct D3DDevice {
  be_u32 m_pRing;                        // +0x000 ring write pointer (EndVertices restores it)
  be_u32 pad_0004;
  be_u32 m_pRingLimit;                   // +0x008
  be_u32 pad_000C;
  be_u64 m_Mask[4];                      // +0x010 dirty masks (+0x10..+0x30); SetStreamSource ORs 0x400 into +0x30
  u8 pad_0030[0x88 - 0x30];
  // Per-state call pointers (RR-NR-2 Q5, medium-high): 87 render states, 20 sampler states, then
  // the Get tables; default {get, set, default} triples at 0x82490700 / 0x82490B18.
  be_u32 m_SetRenderStateCall[87];       // +0x088
  be_u32 m_SetSamplerStateCall[20];      // +0x1E4 (sub_8237F210 calls dev+0x1E4..)
  be_u32 m_GetRenderStateCall[87];       // +0x234 (medium)
  be_u32 m_GetSamplerStateCall[20];      // +0x390 (medium)
  u8 pad_03E0[0x480 - 0x3E0];
  DeviceFetchConstant fetchConstants[32];  // +0x480  SetTexture: device + (sampler + 48) * 24
  be_f32 vsFloatConstants[256][4];       // +0x780  SetVertexShaderConstantF sub_8207B170
  be_f32 psFloatConstants[256][4];       // +0x1780 SetPixelShaderConstantF sub_8207B2B8
  be_u32 vsBoolConstants[4];             // +0x2780 SetVertexShaderConstantB sub_8207B400
  be_u32 psBoolConstants[4];             // +0x2790 the draw flush sends 8 bool dwords (0x4900..) from +0x2780
  u8 pad_27A0[0x289C - 0x27A0];
  be_u32 lastFence;                      // +0x289C SetStreamSource copies it into an unbound VB's Fence
  u8 pad_28A0[0x2C90 - 0x28A0];
  be_u32 vertexDeclaration;              // +0x2C90 SetVertexDeclaration sub_8207BE10
  u8 pad_2C94[0x2CC0 - 0x2C94];
  // Xenos register shadows: one dword per register, in the blocks the draw flush sub_82077A98 sends
  // through sub_82076640 (RR-NR-5): 0x2000 x16 +0x2CC0, 0x2080 x3 +0x2D00, 0x2100 x21 +0x2D0C,
  // 0x2180 x5 +0x2D60, 0x2200 x12 +0x2D74, 0x2280 x21 +0x2DA4, 0x2300 x38 +0x2DF8, 0x2380 x8 +0x2E90
  // = SvR 2010 / UFC's table + 0x440. gpu/draw.cpp SvrRegisterOffset reads them by register.
  be_u32 regs2000[16];                   // +0x2CC0
  be_u32 regs2080[3];                    // +0x2D00 window offset, window scissor TL / BR
  be_u32 regs2100[21];                   // +0x2D0C (0x2102 VGT_INDX_OFFSET +0x2D14; PA_CL_VPORT +0x2D48..)
  be_u32 regs2180[5];                    // +0x2D60
  be_u32 rbDepthControl;                 // +0x2D74 0x2200 RB_DEPTHCONTROL
  be_u32 rbBlendControl0;                // +0x2D78 0x2201 RB_BLENDCONTROL0
  be_u32 rbColorControl;                 // +0x2D7C 0x2202 RB_COLORCONTROL
  be_u32 regs2203[6];                    // +0x2D80 0x2203..0x2208
  be_u32 rbBlendControl1;                // +0x2D98 0x2209
  be_u32 rbBlendControl2;                // +0x2D9C 0x220A
  be_u32 rbBlendControl3;                // +0x2DA0 0x220B
  be_u32 regs2280[21];                   // +0x2DA4
  be_u32 regs2300[38];                   // +0x2DF8
  be_u32 regs2380[8];                    // +0x2E90
  u8 pad_2EB0[0x30F4 - 0x2EB0];
  be_u32 indexBuffer;                    // +0x30F4 SetIndices sub_82075760
  be_u32 renderTargetShadow[4];          // +0x30F8 SetRenderTarget sub_82075C80 ((idx + 3134) * 4)
  be_u32 depthStencilShadow;             // +0x3108 SetDepthStencilSurface sub_82075FE8
  RrStreamSource streams[16];            // +0x310C SetStreamSource sub_82075660 (+0x310C + 8 * stream)
  be_u32 pad_318C;                       // +0x318C no D3D function names it (RR-NR-5 census logs its values)
  u8 streamStrideDiv4[16];               // +0x3190 stride / 4 per stream
  be_u32 boundTexture[16];               // +0x31A0 SetTexture sub_8206D0B8 ((3176 + sampler) * 4)
  u8 pad_31E0[0x3208 - 0x31E0];          //         (vertex samplers past 16? unproven)
  D3DViewport9 viewport;                 // +0x3208 SetViewport sub_820753F0 (integer X/Y/W/H)
  D3DRect scissorRect;                   // +0x3220 SetScissorRect sub_82075328
  u8 pad_3230[0x3290 - 0x3230];
  be_u32 pixelShader;                    // +0x3290 SetPixelShader sub_8207B6C0 (RR-NR-6: PS first, as UFC)
  be_u32 vertexShader;                   // +0x3294 SetVertexShader body sub_8207B8D8
  u8 pad_3298[0x3554 - 0x3298];
  be_u32 savedRingEnd;                   // +0x3554 EndVertices sub_822E0480 sets the ring to it
  u8 pad_3558[0x3608 - 0x3558];
  u8 presentParams[0x3698 - 0x3608];     // +0x3608 Swap sub_820804F0 argument (size: up to the buffers)
  be_u32 frontBuffer;                    // +0x3698 Present resolves into it, Swap shows it
  be_u32 backBuffer;                     // +0x369C Present: SetRenderTarget(0, back)
  u8 pad_36A0[0x4FFC - 0x36A0];
  be_u32 vertexShaderPublic;             // +0x4FFC SetVertexShader sub_8207BA30 (public entry)
  u8 pad_5000[0x5070 - 0x5000];
  be_u32 createFlags;                    // +0x5070 Direct3D_CreateDevice
  u8 pad_5074[0x5080 - 0x5074];
};
static_assert(sizeof(D3DDevice) == 0x5080);
static_assert(sizeof(D3DDevice) == kD3DDeviceAllocSize);
static_assert(offsetof(D3DDevice, m_pRingLimit) == 0x008);
static_assert(offsetof(D3DDevice, m_Mask) == 0x010);
static_assert(offsetof(D3DDevice, m_SetRenderStateCall) == 0x088);
static_assert(offsetof(D3DDevice, m_SetSamplerStateCall) == 0x1E4);
static_assert(offsetof(D3DDevice, m_GetRenderStateCall) == 0x234);
static_assert(offsetof(D3DDevice, m_GetSamplerStateCall) == 0x390);
static_assert(offsetof(D3DDevice, fetchConstants) == 0x480);
static_assert(offsetof(D3DDevice, vsFloatConstants) == 0x780);
static_assert(offsetof(D3DDevice, psFloatConstants) == 0x1780);
static_assert(offsetof(D3DDevice, vsBoolConstants) == 0x2780);
static_assert(offsetof(D3DDevice, lastFence) == 0x289C);
static_assert(offsetof(D3DDevice, vertexDeclaration) == 0x2C90);
static_assert(offsetof(D3DDevice, indexBuffer) == 0x30F4);
static_assert(offsetof(D3DDevice, renderTargetShadow) == 0x30F8);
static_assert(offsetof(D3DDevice, depthStencilShadow) == 0x3108);
static_assert(offsetof(D3DDevice, psBoolConstants) == 0x2790);
static_assert(offsetof(D3DDevice, regs2000) == 0x2CC0);
static_assert(offsetof(D3DDevice, regs2100) == 0x2D0C);
static_assert(offsetof(D3DDevice, rbDepthControl) == 0x2D74);
static_assert(offsetof(D3DDevice, rbBlendControl0) == 0x2D78);
static_assert(offsetof(D3DDevice, rbBlendControl1) == 0x2D98);
static_assert(offsetof(D3DDevice, regs2280) == 0x2DA4);
static_assert(offsetof(D3DDevice, regs2380) == 0x2E90);
static_assert(offsetof(D3DDevice, streams) == 0x310C);
static_assert(offsetof(D3DDevice, streamStrideDiv4) == 0x3190);
static_assert(offsetof(D3DDevice, boundTexture) == 0x31A0);
static_assert(offsetof(D3DDevice, viewport) == 0x3208);
static_assert(offsetof(D3DDevice, scissorRect) == 0x3220);
static_assert(offsetof(D3DDevice, pixelShader) == 0x3290);
static_assert(offsetof(D3DDevice, vertexShader) == 0x3294);
static_assert(offsetof(D3DDevice, savedRingEnd) == 0x3554);
static_assert(offsetof(D3DDevice, presentParams) == 0x3608);
static_assert(offsetof(D3DDevice, frontBuffer) == 0x3698);
static_assert(offsetof(D3DDevice, backBuffer) == 0x369C);
static_assert(offsetof(D3DDevice, vertexShaderPublic) == 0x4FFC);
static_assert(offsetof(D3DDevice, createFlags) == 0x5070);

// UFC's device.streamSource[n] holds the vertex buffer itself; RR keeps {offset, vb} pairs.
// Ported readers use these instead of touching streamSource.
inline u32 StreamVertexBuffer(const D3DDevice &d, u32 stream) { return d.streams[stream].vertexBuffer; }
inline u32 StreamOffset(const D3DDevice &d, u32 stream) { return d.streams[stream].offset; }
#elif defined(SVR_NATIVE_RENDERER)
// UFC 3 D3DDevice: 0x5F80 bytes (Direct3D_CreateDevice sub_82374F80 allocates 24448). The
// constants block and register shadows match SvR 2010 (to +0x2AC0); the later fields are UFC's own
// (src/native/ufc_d3d.h names the function whose store proves each one): vertex declaration
// SvR + 0x80, the index buffer .. shaders block SvR + 0x48. Only the fields the native renderer
// reads or writes are named.
struct SvrViewport {
  be_f32 X, Y, Width, Height, MinZ, MaxZ;  // SetViewport sub_82377F58 stores floats
};
static_assert(sizeof(SvrViewport) == 24);

struct D3DDevice {
  be_u64 m_Mask[5];                     // +0x000
  u8 pad_0028[0x480 - 0x28];
  DeviceFetchConstant fetchConstants[32];  // +0x480  SetTexture: device + (sampler+48)*24
  be_f32 vsFloatConstants[256][4];      // +0x780  draws flush 0x4000.. from device+0x780
  be_f32 psFloatConstants[256][4];      // +0x1780 draws flush 0x4400.. from device+0x1780
  be_u32 vsBoolConstants[4];            // +0x2780 (constants block +0x80, as above)
  be_u32 psBoolConstants[4];            // +0x2790
  u8 pad_27A0[0x2938 - 0x27A0];
  be_u32 rbBlendControl0;               // +0x2938 register 0x2201 (SetRenderState_AlphaBlendEnable)
  u8 pad_293C[0x2958 - 0x293C];
  be_u32 rbBlendControl1;               // +0x2958 0x2209
  be_u32 rbBlendControl2;               // +0x295C 0x220A
  be_u32 rbBlendControl3;               // +0x2960 0x220B
  u8 pad_2964[0x2F58 - 0x2964];
  be_u32 vertexDeclaration;             // +0x2F58 SetVertexDeclaration sub_82371BB8
  u8 pad_2F5C[0x318C - 0x2F5C];
  be_u32 indexBuffer;                   // +0x318C SetIndices sub_82377900
  be_u32 renderTargetShadow[4];         // +0x3190 SetRenderTarget sub_82378088
  be_u32 depthStencilShadow;            // +0x31A0 SetDepthStencilSurface sub_82378418
  // SetStreamSource (sub_82377758): stwx vb,(3177+stream)*4 and the stride>>2 byte at
  // +0x31E8+stream. The stream's fetch constant goes to fetchConstants (vertex fetch constant 95 - stream).
  be_u32 streamSource[16];              // +0x31A4 D3DVertexBuffer per stream
  u8 pad_31E4[0x31E8 - 0x31E4];
  u8 streamStrideDiv4[16];              // +0x31E8 stride / 4 per stream
  u8 pad_31F8[0x3260 - 0x31F8];         // +0x31F8 SetTexture's texture per sampler
  SvrViewport viewport;                 // +0x3260 SetViewport
  be_u32 viewportFlags;                 // +0x3278
  D3DRect scissorRect;                  // +0x327C SetScissorRect sub_82377610
  be_u32 pixelShader;                   // +0x328C SetPixelShader sub_82371798
  be_u32 vertexShader;                  // +0x3290 SetVertexShader sub_823719A0
  u8 pad_3294[0x5F80 - 0x3294];
};
static_assert(sizeof(D3DDevice) == 0x5F80);
static_assert(offsetof(D3DDevice, fetchConstants) == 0x480);
static_assert(offsetof(D3DDevice, vsFloatConstants) == 0x780);
static_assert(offsetof(D3DDevice, psFloatConstants) == 0x1780);
static_assert(offsetof(D3DDevice, vsBoolConstants) == 0x2780);
static_assert(offsetof(D3DDevice, rbBlendControl0) == 0x2938);
static_assert(offsetof(D3DDevice, vertexDeclaration) == 0x2F58);
static_assert(offsetof(D3DDevice, indexBuffer) == 0x318C);
static_assert(offsetof(D3DDevice, renderTargetShadow) == 0x3190);
static_assert(offsetof(D3DDevice, depthStencilShadow) == 0x31A0);
static_assert(offsetof(D3DDevice, streamSource) == 0x31A4);
static_assert(offsetof(D3DDevice, streamStrideDiv4) == 0x31E8);
static_assert(offsetof(D3DDevice, viewport) == 0x3260);
static_assert(offsetof(D3DDevice, scissorRect) == 0x327C);
static_assert(offsetof(D3DDevice, pixelShader) == 0x328C);
static_assert(offsetof(D3DDevice, vertexShader) == 0x3290);
#else
struct D3DDevice {
  // m_Pending (_D3DTAGCOLLECTION): 5 dirty mask u64s. Set* setters OR into
  // m_Mask[2] (byte +0x10), SetSamplerStateInline into m_Mask[3].
  be_u64 m_Mask[5];        // +0x000
  be_u32 m_pRing;          // +0x028
  be_u32 m_pRingLimit;     // +0x02C
  be_u32 m_pRingGuarantee; // +0x030
  be_u32 m_ReferenceCount; // +0x034
  // Per-state dispatch tables, seeded by Direct3D_CreateDevice_hook /
  // CopyDispatchTable from the XEX template tables.
  be_u32 m_SetRenderStateCall[97];  // +0x038
  be_u32 m_SetSamplerStateCall[20]; // +0x1BC
  be_u32 m_GetRenderStateCall[97];  // +0x20C
  be_u32 m_GetSamplerStateCall[20]; // +0x390
  u8 pad_03E0[0x400 - 0x3E0];       // +0x3E0
  // m_Constants (_D3DConstants, +0x400..+0x27A0). The unified Xenos fetch
  // table: 32 slots shared by texture (type 2) and vertex (type 3) fetch
  // constants, flushed by CommitTextureFetchConstants 0x82486018.
  DeviceFetchConstant fetchConstants[32]; // +0x400
  be_f32 vsFloatConstants[256][4];        // +0x700
  be_f32 psFloatConstants[256][4];        // +0x1700
  // VS / PS boolean constants, 4 u32 (128 bits) each.
  // D3DDevice_SetVertexShaderConstantB writes dword (N/32)+2496 =
  // device+0x2700, and SetPixelShaderConstantB writes +2500 =
  // device+0x2710. The rest of the _D3DConstants tail is int/loop constants.
  be_u32 vsBoolConstants[4];    // +0x2700
  be_u32 psBoolConstants[4];    // +0x2710
  u8 pad_2720[0x27A0 - 0x2720]; // +0x2720  int/loop constants
  be_f32 m_ClipPlanes[6][4];    // +0x27A0
  u8 pad_2800[0x28B8 - 0x2800]; // +0x2800  GPU packets + guest tail
  // RB_BLENDCONTROL0..3 (Xenos blend ROP state), seeded to no-blend. Slot 0 is
  // non-contiguous with the 1..3 trio.
  be_u32 rbBlendControl0;       // +0x28B8
  u8 pad_28BC[0x28D8 - 0x28BC]; // +0x28BC
  be_u32 rbBlendControl1;       // +0x28D8
  be_u32 rbBlendControl2;       // +0x28DC
  be_u32 rbBlendControl3;       // +0x28E0
  u8 pad_28E4[0x2A39 - 0x28E4]; // +0x28E4
  // "Resolution applied" bit. D3DDevice_Reset/SetResolution ORs in 0x10.
  u8 resolutionApplied;         // +0x2A39
  u8 pad_2A3A[0x2D10 - 0x2A3A]; // +0x2A3A
  // D3DDevice_SetVertexDeclaration: stw r4, 0x2D10(r3).
  be_u32 vertexDeclaration;     // +0x2D10
  u8 pad_2D14[0x2D3C - 0x2D14]; // +0x2D14
  // Xenos RB_COLORCONTROL shadow. Bit 31 is the alpha blend master enable.
  // ReadDeviceRenderState reads regs[2895] (2895*4 = 0x2D3C). The regs[] index
  // == device byte offset / 4 mapping holds because regs[2606]*4 = 0x28B8, the
  // rbBlendControl0 offset asserted below.
  be_u32 rbColorControl;        // +0x2D3C
  u8 pad_2D40[0x2F88 - 0x2D40]; // +0x2D40
  // Bound RT/DS surface shadow. D3DDevice_GetRenderTarget (0x824739F0) reads
  // +0x2F88 + idx*4, and D3DDevice_GetDepthStencilSurface (0x82473A38) reads
  // +0x2F98. Hooks replaced the recompiled Set* bodies that wrote these, so the
  // hooks must maintain them or the RT stack save/restore
  // (bdRenderTargetStackPush/Pop 0x82273080/0x82273240, every Sofdec movie
  // frame) restores garbage.
  be_u32 renderTargetShadow[4]; // +0x2F88
  be_u32 depthStencilShadow;    // +0x2F98
  u8 pad_2F9C[0x3058 - 0x2F9C]; // +0x2F9C
  // D3DDevice_SetViewport stores X/Y/W/H/MinZ/MaxZ at +0x3058 and
  // the scissor rect at +0x3070.
  D3DViewport9 viewport; // +0x3058
  D3DRect scissorRect;   // +0x3070
  // BD-private slots past the public 0x2A00 struct.
  // D3DDevice_SetPixelShader: stw r29, 0x3080(r30).
  // D3DDevice_SetVertexShader: stw r29, 0x3084(r30).
  be_u32 pixelShader;           // +0x3080
  be_u32 vertexShader;          // +0x3084
  u8 pad_3088[0x3370 - 0x3088]; // +0x3088
  // D3DPRESENT_PARAMETERS copy written verbatim by D3DDevice_Reset (124 bytes).
  u8 presentParams[124];        // +0x3370
  u8 pad_33EC[0x4D38 - 0x33EC]; // +0x33EC
  // D3DDevice_SetIndices (0x8247B328): stw r30, 0x4D38(r31).
  be_u32 indexBuffer;           // +0x4D38
  u8 pad_4D3C[0x4DFC - 0x4D3C]; // +0x4D3C
  // Resolution block mirrored by D3DDevice_Reset
  // (D3D::InitializePresentationParameters): width, height, width(dup),
  // config flag.
  be_u32 resolutionWidth;       // +0x4DFC
  be_u32 resolutionHeight;      // +0x4E00
  be_u32 resolutionWidthDup;    // +0x4E04
  be_u32 resolutionConfig;      // +0x4E08
  u8 pad_4E0C[0x5000 - 0x4E0C]; // +0x4E0C
};
static_assert(sizeof(D3DDevice) == 0x5000);
static_assert(offsetof(D3DDevice, m_pRing) == 0x028);
static_assert(offsetof(D3DDevice, m_SetRenderStateCall) == 0x038);
static_assert(offsetof(D3DDevice, m_SetSamplerStateCall) == 0x1BC);
static_assert(offsetof(D3DDevice, m_GetRenderStateCall) == 0x20C);
static_assert(offsetof(D3DDevice, m_GetSamplerStateCall) == 0x390);
static_assert(offsetof(D3DDevice, fetchConstants) == 0x400);
static_assert(offsetof(D3DDevice, vsFloatConstants) == 0x700);
static_assert(offsetof(D3DDevice, psFloatConstants) == 0x1700);
static_assert(offsetof(D3DDevice, vsBoolConstants) == 0x2700);
static_assert(offsetof(D3DDevice, psBoolConstants) == 0x2710);
static_assert(offsetof(D3DDevice, m_ClipPlanes) == 0x27A0);
static_assert(offsetof(D3DDevice, vertexDeclaration) == 0x2D10);
static_assert(offsetof(D3DDevice, rbColorControl) == 0x2D3C);
static_assert(offsetof(D3DDevice, renderTargetShadow) == 0x2F88);
static_assert(offsetof(D3DDevice, depthStencilShadow) == 0x2F98);
static_assert(offsetof(D3DDevice, viewport) == 0x3058);
static_assert(offsetof(D3DDevice, scissorRect) == 0x3070);
static_assert(offsetof(D3DDevice, pixelShader) == 0x3080);
static_assert(offsetof(D3DDevice, vertexShader) == 0x3084);
static_assert(offsetof(D3DDevice, indexBuffer) == 0x4D38);
static_assert(offsetof(D3DDevice, resolutionApplied) == 0x2A39);
static_assert(offsetof(D3DDevice, presentParams) == 0x3370);
static_assert(offsetof(D3DDevice, resolutionWidth) == 0x4DFC);
static_assert(offsetof(D3DDevice, rbBlendControl0) == 0x28B8);
static_assert(offsetof(D3DDevice, rbBlendControl1) == 0x28D8);
static_assert(offsetof(D3DDevice, rbBlendControl2) == 0x28DC);
static_assert(offsetof(D3DDevice, rbBlendControl3) == 0x28E0);
#endif  // RRXX_NATIVE_RENDERER / SVR_NATIVE_RENDERER

#if !defined(RRXX_NATIVE_RENDERER)
// D3DResource_GetType decodes the type from D3DResource:
//   base = Common & 0xF
//     1 -> Surface  (extended to 16 if Common & 0x40000000 + texture-backed)
//     3 -> Texture, then dim = (fetch_dword5 >> 9) & 3:
//            0 -> returns 20 (1D / line texture)
//            1 -> returns 3 if !tiled, 19 if tiled (fetch_dword1 & 0x400)
//            2 -> returns 17 (volume texture)
//            3 -> returns 18 (stacked cube)
//     6 -> VertexBuffer
//     7 -> IndexBuffer
// The Common low-4 bits select the base type. Texture dimensionality lives in
// the fetch constant. Bit 30 (0x40000000) on a surface marks it texture-backed,
// which routes D3DSurface_GetDesc through D3DTexture_GetLevelDesc.
constexpr u32 kCommonFlagTextureBackedSurface = 0x40000000u;
#endif  // !RRXX_NATIVE_RENDERER (RR: a level surface has type kSurface and a Parent, see D3DSurface)

constexpr u32 kClearTarget = 0x1u;
constexpr u32 kClearZBuffer = 0x10u;
constexpr u32 kClearStencil = 0x20u;
constexpr u32 kClearAll = kClearTarget | kClearZBuffer | kClearStencil;

constexpr u32 kResolveDepthStencil = 0x4u;
constexpr u32 kResolveExponentBiasShift = 26;

#if defined(RRXX_NATIVE_RENDERER)
// As RR's Create* functions leave a new resource: refcount 1, type, created bit, BaseFlush 0xFFFF0000.
inline void InitResourceHeader(D3DResource &r, D3DResourceType type) {
  r.Common = kCommonCreated | (u32(type) << kCommonTypeShift) | 1u;
  r.Fence = 0u;
  r.BaseFlush = 0xFFFF0000u;
}
#else
inline void InitResourceHeader(D3DResource &r, D3DResourceType type) {
  r.Common = u32(type);
  r.ReferenceCount = 1u;
  r.Fence = 0u;
  r.ReadFence = 0u;
  r.Identifier = 0u;
  r.BaseFlush = 0u;
}
#endif  // RRXX_NATIVE_RENDERER

} // namespace bd::gpu
