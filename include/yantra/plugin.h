/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Yantra public plugin API (PLG-01, OA-01).
 *
 * Plain C so plugins can be written in C, C++ or Rust. This is the minimal
 * versioned stub of the build skeleton: it fixes the version macros and the
 * entry-point name only. The full C ABI (plugin kinds, data model, host
 * services, UI interface; PLG-06) replaces it and bumps the version.
 * Radio plugins use yantra-radios' own header yantra/radio.h instead
 * (PLG-26). Copied from yantra-v2 68ea20a (9540c71): include/yantra/plugin.h.
 *
 * Versioning: a host loads a plugin when the major versions are equal and
 * the plugin's minor version is not newer than the host's.
 */
#ifndef YANTRA_PLUGIN_H
#define YANTRA_PLUGIN_H

#include <stdint.h>

#define YANTRA_PLUGIN_ABI_VERSION_MAJOR 0u
#define YANTRA_PLUGIN_ABI_VERSION_MINOR 1u

/* Name of the symbol every plugin exports (see yantra_plugin_entry_fn). */
#define YANTRA_PLUGIN_ENTRY_SYMBOL "yantra_plugin_entry"

#if defined(_WIN32)
#  define YANTRA_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define YANTRA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ABI version a plugin was built against. */
typedef struct yantra_plugin_abi_version {
    uint32_t major;
    uint32_t minor;
} yantra_plugin_abi_version;

/* Signature of the exported entry point. Returns the ABI version the plugin
 * was built against; phase 2 extends this with the plugin descriptor. */
typedef yantra_plugin_abi_version (*yantra_plugin_entry_fn)(void);

/* Nonzero when a plugin built against `plugin` can be loaded by a host
 * built against `host`. */
static inline int yantra_plugin_abi_compatible(yantra_plugin_abi_version host,
                                               yantra_plugin_abi_version plugin)
{
    return host.major == plugin.major && plugin.minor <= host.minor;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_PLUGIN_H */
