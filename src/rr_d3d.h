// Rumble Roses XX's XDK D3DDevice layout as offsets: RR's counterpart of UFC-native's
// src/native/ufc_d3d.h (RR-NR-3). The same layout as a struct is reblue/gpu/d3d.h's
// RRXX_NATIVE_RENDERER branch; static_asserts below keep the two equal.
//
// RR links XDK D3D 2.0.2500, older than UFC's and SvR's (reports/FINDINGS-rr-nr1.md). Device =
// 0x5080 bytes (Direct3D_CreateDevice sub_820706F0, li r3,20608). Each offset names the function
// whose store proves it. Offline evidence only: the census (Stop 1) checks every one at run time.
// "med" = medium confidence in FINDINGS-rr-nr1.md / PLAN-rr-nr2.md Q5.

#pragma once

#include <cstdint>

namespace rrxx::d3d {

constexpr uint32_t kDeviceSize = 0x5080;

// Ring and dirty masks.
constexpr uint32_t kRing = 0x000;               // write pointer
constexpr uint32_t kRingLimit = 0x008;
constexpr uint32_t kDirtyMasks = 0x010;         // be_u64 x4 (+0x10..+0x30); SetStreamSource ORs 0x400 into +0x30

// Per-state call pointers (med): the game calls render / sampler states through them
// (sub_8237F210: dev+0x1E4..); defaults {get, set, default} at 0x82490700 (87) / 0x82490B18 (20).
constexpr uint32_t kSetRenderStateCall = 0x088;   // [87]
constexpr uint32_t kSetSamplerStateCall = 0x1E4;  // [20]
constexpr uint32_t kGetRenderStateCall = 0x234;   // [87] (med)
constexpr uint32_t kGetSamplerStateCall = 0x390;  // [20] (med)

// Constants block: where UFC has it.
constexpr uint32_t kFetchConstants = 0x480;     // SetTexture sub_8206D0B8: (sampler + 48) * 24
constexpr uint32_t kVsFloatConstants = 0x780;   // SetVertexShaderConstantF sub_8207B170
constexpr uint32_t kPsFloatConstants = 0x1780;  // SetPixelShaderConstantF sub_8207B2B8
constexpr uint32_t kVsBoolConstants = 0x2780;   // SetVertexShaderConstantB sub_8207B400 (med)
constexpr uint32_t kLastFence = 0x289C;         // SetStreamSource copies it into the old VB's Fence
// Register shadows ~+0x2CB8..+0x2F7C (PA_CL_VPORT +0x2D48..+0x2D5C from SetViewport, scissor
// +0x2D04/+0x2D08, base vertex +0x2D14 from DrawIndexedVertices): unproven until the census.
constexpr uint32_t kBaseVertexIndex = 0x2D14;   // DrawIndexedVertices sub_822E06E0 (VGT_INDX_OFFSET shadow)

constexpr uint32_t kVertexDeclaration = 0x2C90; // SetVertexDeclaration sub_8207BE10 (UFC +0x2F58)
constexpr uint32_t kIndexBuffer = 0x30F4;       // SetIndices sub_82075760 (UFC +0x318C)
constexpr uint32_t kRenderTarget0 = 0x30F8;     // SetRenderTarget sub_82075C80: (idx + 3134) * 4, 4 slots
constexpr uint32_t kDepthStencil = 0x3108;      // SetDepthStencilSurface sub_82075FE8 (UFC +0x31A0)
constexpr uint32_t kStreamSource = 0x310C;      // SetStreamSource sub_82075660: {offset, vb} at +8 * stream
constexpr uint32_t kStreamSourceStride = 8;     //   (UFC: bare vb, 4 bytes, at +0x31A4)
constexpr uint32_t kStreamStrideDiv4 = 0x3190;  // u8 per stream
constexpr uint32_t kTexture = 0x31A0;           // SetTexture: (3176 + s) * 4 (UFC +0x31F8)
constexpr uint32_t kViewport = 0x3208;          // SetViewport sub_820753F0: integer D3DVIEWPORT9 (24 bytes)
constexpr uint32_t kScissorRect = 0x3220;       // SetScissorRect sub_82075328, clamped to the viewport
constexpr uint32_t kPixelShader = 0x3290;       // SetPixelShader sub_8207B6C0 (RR-NR-6: PS before VS, as UFC)
constexpr uint32_t kVertexShader = 0x3294;      // SetVertexShader body sub_8207B8D8
constexpr uint32_t kSavedRingEnd = 0x3554;      // EndVertices sub_822E0480: ring = saved end
constexpr uint32_t kPresentParams = 0x3608;     // Swap sub_820804F0 argument
constexpr uint32_t kFrontBuffer = 0x3698;       // Present resolves into it; Swap shows it
constexpr uint32_t kBackBuffer = 0x369C;        // Present: SetRenderTarget(0, +0x369C)
constexpr uint32_t kVertexShaderPublic = 0x4FFC; // SetVertexShader sub_8207BA30 (public entry)
constexpr uint32_t kCreateFlags = 0x5070;       // Direct3D_CreateDevice

// Resource objects (gpu/d3d.h has them as structs).
constexpr uint32_t kResourceHeaderSize = 12;    // Common, Fence, BaseFlush; refcount = Common & 0xFF
constexpr uint32_t kVertexBufferSize = 20;      // CreateVertexBuffer sub_8206D878
constexpr uint32_t kIndexBufferSize = 20;       // CreateIndexBuffer sub_8206D9A8
constexpr uint32_t kBufferAddress = 0x0C;       // VB: address | 3 (fetch dword 0); IB: address
constexpr uint32_t kBufferSize = 0x10;          // VB: size | 2; IB: bytes
constexpr uint32_t kTextureSize = 40;           // CreateTexture sub_8206CBA8
constexpr uint32_t kTextureFetch = 0x10;        // 6-dword fetch constant (SetTexture copies it)
constexpr uint32_t kSurfaceSize = 64;           // CreateSurface sub_8206CCC8
constexpr uint32_t kSurfaceParent = 0x2C;       // AddRef / Release forward to it for a level surface
constexpr uint32_t kPixelShaderObjectSize = 52; // CreatePixelShader sub_8207BC48
constexpr uint32_t kVertexShaderPrefix = 592;   // CreateVertexShader sub_8207BD38
constexpr uint32_t kVertexDeclBase = 40;        // CreateVertexDeclaration sub_8207BA98: 40 + 12 * n

// Resolve (sub_82070F48): flags & 7 = render target index, 4 = depth.
constexpr uint32_t kResolveTargetMask = 7;

static_assert(kVertexShader == kPixelShader + 4);
static_assert(kStreamSource == kDepthStencil + 4);
static_assert(kStreamSource + 16 * kStreamSourceStride + 4 == kStreamStrideDiv4);  // one unknown dword between
static_assert(kTexture == kStreamStrideDiv4 + 16);
static_assert(kScissorRect == kViewport + 24);
static_assert(kRenderTarget0 == kIndexBuffer + 4 && kDepthStencil == kRenderTarget0 + 16);
static_assert(kCreateFlags < kDeviceSize && kVertexShaderPublic < kDeviceSize);

}  // namespace rrxx::d3d
