// Native renderer switch (RR port of UFC-native src/native/ufc_native.h).
//
// rrxx_renderer (emulated | native) is read once in RumbleRosesXxApp::LatchRenderer
// (src/rumble_roses_xx_app.h in the game tree) and latched in g_rrxx_native. Every native hook
// starts with `if (!g_ufc3_native) ...` (the UFC name, kept so the ported sources read like UFC's),
// so with the default (emulated) the game runs exactly as the v1.5c exe.

#pragma once

#ifndef RRXX_NATIVE_LATCH_DEFINED
#define RRXX_NATIVE_LATCH_DEFINED
inline bool g_rrxx_native = false;
#endif
#define g_ufc3_native g_rrxx_native
