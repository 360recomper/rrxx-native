// Rumble Roses XX addresses for the XDK D3D functions that re:Blue's renderer hooks by name
// (RR-NR-3, from research/rr-nr1/rr_d3d_names.draft.h; template UFC-native src/native/ufc_d3d_names.h).
//
// Force-included into the native renderer sources (native_renderer.cmake), so
// REX_HOOK(D3DDevice_Clear, ...) overrides RR's recompiled sub_820704E0 and __imp__D3DDevice_Clear
// calls its original body. Base exe = RRXX default.xex without the title update (TU #2 moves every
// address; the native rewrite bakes TU in later, see memory title-update). The map is
// research notes (a): matched by call shape and the device fields each function writes;
// "med"/"low" there = to be checked at run time by the census (Stop 1). Functions not listed are
// either not used by RR or not present in its XDK (2.0.2500); a hook that names one fails to compile
// on purpose.
//
// None of RR's 83 game midasm hooks sits in the D3D runtime (0x8206B000-0x82081000,
// 0x822E0000-0x822E0A00), so every name below is free to hook.

#pragma once

// Device / frame
#define D3DDevice_Present                     sub_82080AD0
#define D3DDevice_Swap                        sub_820804F0  // only VdSwap caller (also called by sub_8207F050)
#define D3DDevice_SynchronizeToPresentationInterval sub_82080380
#define D3DDevice_Resolve                     sub_82070F48  // 11-arg form: ClearStencil + params on the caller's stack
#define D3DDevice_Clear                       sub_820704E0  // loops its rects into ClearF
#define D3DDevice_ClearF                      sub_8206FDB0  // internal per-rect clear (med)
#define D3DDevice_SetRenderTarget             sub_82075C80  // the game calls the 1-instruction alias sub_820761B8
#define D3DDevice_SetDepthStencilSurface      sub_82075FE8
#define D3DDevice_SetScissorRect              sub_82075328  // internal callers only
#define D3DDevice_SetViewport                 sub_820753F0  // integer D3DVIEWPORT9
// D3DDevice_SetViewportF: not in this XDK.

// Draws. RR has no BeginIndexedVertices and no DrawVerticesUP (only 3 DRAW_INDX writers):
// leave those hooks unmapped.
#define D3DDevice_BeginVertices               sub_822E01E0
#define D3DDevice_EndVertices                 sub_822E0480
#define D3DDevice_DrawVertices                sub_822E04A0
#define D3DDevice_DrawIndexedVertices         sub_822E06E0

// Buffers: the game creates, locks and unlocks its own; the renderer only observes Unlock.
// D3DDevice_CreateVertexBuffer sub_8206D878 (20-byte object), D3DDevice_CreateIndexBuffer
// sub_8206D9A8, D3DVertexBuffer_Lock sub_8206D928 and D3DIndexBuffer_Lock sub_8206DA58 are matched
// but deliberately NOT mapped: gpu/hooks/resource.cpp's REX_HOOKs would replace them (as UFC).
#define D3DVertexBuffer_Unlock                sub_8206D998  // address = FetchLo (+0x0C) & ~3
#define D3DIndexBuffer_Unlock                 sub_8206DAC0

// Resource creation
#define D3DDevice_CreateVertexDeclaration     sub_8207BA98  // 40 + 12n bytes
#define D3DDevice_CreateVertexShader          sub_8207BD38  // 592-byte object, 0x102A0E container
#define D3DDevice_CreatePixelShader           sub_8207BC48  // 52-byte object
// Also matched, not mapped (no re:Blue hook needs them yet): CreateTexture sub_8206CBA8 (40 bytes),
// CreateSurface sub_8206CCC8 (64), GetSurfaceLevel 2D/cube/volume sub_8206C980 / sub_8206CA40 /
// sub_8206CAF8, Texture LockRect sub_8206CA28 / UnlockRect sub_8206B0D8, Resource AddRef
// sub_8206D210 / Release sub_8206D790 / Destroy sub_8206D328, KickOff / MakeSpace sub_8206F250,
// fence / callback packet sub_8206F510.

// State
#define D3DDevice_SetTexture                  sub_8206D0B8  // table +0x31A0, fetch +0x480 + 24 * (48 + s)
// RR-NR-6: FINDINGS-rr-nr1 had these two swapped. sub_8207B6C0 ORs the shader's constant use into the
// PS constant mask (+0x18) and stores +0x3290: SetPixelShader. sub_8207BA30 stores +0x4FFC and calls
// sub_8207B8D8, which ORs into the VS constant mask (+0x10) and stores +0x3294: SetVertexShader. (The
// native run nr6b built every pipeline with VS and PS swapped: E_INVALIDARG.)
#define D3DDevice_SetVertexShader             sub_8207BA30  // public entry (+0x4FFC); body sub_8207B8D8 stores +0x3294
#define D3DDevice_SetPixelShader              sub_8207B6C0  // stores +0x3290
#define D3DDevice_SetVertexDeclaration        sub_8207BE10
#define D3DDevice_SetStreamSource             sub_82075660  // {offset, vb} pairs, not UFC's bare vb
#define D3DDevice_SetIndices                  sub_82075760
#define D3DDevice_SetVertexShaderConstantF    sub_8207B170  // 0 direct callers (pointer stored by sub_82298808)
#define D3DDevice_SetPixelShaderConstantF     sub_8207B2B8  // 0 direct callers

// Recorded work: none in RR. No command buffers, ZPass, predicated tiling, queries or
// SetShaderGPRAllocation (no INDIRECT_BUFFER outside the ring code sub_8206E710, no VIZ_QUERY or
// extra DRAW_INDX writers). UFC's hooks for them stay unmapped.

// Original bodies (__imp__ = the recompiled function, for hooks that call through).
#define __imp__D3DDevice_Present              __imp__sub_82080AD0
#define __imp__D3DDevice_Swap                 __imp__sub_820804F0
#define __imp__D3DDevice_Resolve              __imp__sub_82070F48
#define __imp__D3DDevice_Clear                __imp__sub_820704E0
#define __imp__D3DDevice_ClearF               __imp__sub_8206FDB0
#define __imp__D3DDevice_SetTexture           __imp__sub_8206D0B8
#define __imp__D3DDevice_SetRenderTarget      __imp__sub_82075C80
#define __imp__D3DDevice_SetDepthStencilSurface __imp__sub_82075FE8
#define __imp__D3DDevice_SetScissorRect       __imp__sub_82075328
#define __imp__D3DDevice_SetViewport          __imp__sub_820753F0
#define __imp__D3DDevice_BeginVertices        __imp__sub_822E01E0
#define __imp__D3DDevice_EndVertices          __imp__sub_822E0480
#define __imp__D3DDevice_DrawVertices         __imp__sub_822E04A0
#define __imp__D3DDevice_DrawIndexedVertices  __imp__sub_822E06E0
#define __imp__D3DDevice_CreateVertexDeclaration __imp__sub_8207BA98
#define __imp__D3DDevice_CreateVertexShader   __imp__sub_8207BD38
#define __imp__D3DDevice_CreatePixelShader    __imp__sub_8207BC48
#define __imp__D3DDevice_SetVertexShader      __imp__sub_8207BA30
#define __imp__D3DDevice_SetPixelShader       __imp__sub_8207B6C0
#define __imp__D3DDevice_SetVertexDeclaration __imp__sub_8207BE10
#define __imp__D3DDevice_SetStreamSource      __imp__sub_82075660
#define __imp__D3DDevice_SetIndices           __imp__sub_82075760
#define __imp__D3DDevice_SetVertexShaderConstantF __imp__sub_8207B170
#define __imp__D3DDevice_SetPixelShaderConstantF  __imp__sub_8207B2B8
#define __imp__D3DVertexBuffer_Unlock         __imp__sub_8206D998
#define __imp__D3DIndexBuffer_Unlock          __imp__sub_8206DAC0

// Resource description getters (med; the re:Blue hooks only need them to link).
#define __imp__D3DSurface_GetDesc             __imp__sub_8206CFD0
#define __imp__D3DTexture_GetLevelDesc        __imp__sub_8206C978  // b sub_8206BE90
#define __imp__D3DVolumeTexture_GetLevelDesc __imp__sub_8206CAF0  // b sub_8206BF38 (writes D3DVolumeDesc; RR-NR-5)
// D3DResource_GetType = sub_8206D278 (Common bits 16-18: 1 VB, 2 IB, 3 texture (dim from fetch dword5), 4 surface): not mapped.
