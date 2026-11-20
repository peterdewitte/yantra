// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * \file yantra/plugin.hpp
 * \brief Header-only C++20 helpers for plugins written in C++ against the
 *        plugin API (yantra/plugin.h): version texts and refusal reasons
 *        (PLG-04), text copying by the buffer rules (PLG-03), and a guard
 *        that keeps exceptions from crossing the C boundary.
 *
 * Nothing here crosses the ABI: the helpers work on the C structs and
 * return C++ values to C++ callers only. The full checks Yantra makes on
 * a descriptor (schemas, scope boundary, licence, UI tables) live in the
 * core library (yantra/core/plugin/descriptor.h) and are run by the
 * plugin test kit.
 */
#ifndef YANTRA_PLUGIN_HPP
#define YANTRA_PLUGIN_HPP

#include <yantra/plugin.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <string_view>

namespace yantra::plugin_api {

/** "1.0" style text of a version. */
inline std::string version_text(yantra_api_version v)
{
    return std::to_string(v.major) + "." + std::to_string(v.minor);
}

/** Size of the frozen descriptor prefix (struct_size, api_version, kinds)
 *  that every major version keeps (PLG-04). */
inline constexpr std::uint32_t descriptor_prefix_size = 16;

/**
 * Why a host built for `host` must refuse the plugin descriptor `d`, or ""
 * when the version allows loading it (PLG-04). Reads only the frozen
 * prefix before it decides, so it is safe for a plugin of any major
 * version.
 */
inline std::string refusal_reason(yantra_api_version host, const yantra_plugin_descriptor* d)
{
    if (d == nullptr)
        return "the plugin returned no descriptor";
    if (d->struct_size < descriptor_prefix_size)
        return "the plugin descriptor is malformed (struct_size " + std::to_string(d->struct_size) +
               ")";
    const yantra_api_version p = d->api_version;
    if (p.major != host.major)
        return "the plugin was built for plugin API " + version_text(p) +
               ", this Yantra supports plugin API " + std::to_string(host.major) +
               ".x; use a version of the plugin built for plugin API " +
               std::to_string(host.major) + ".x";
    if (p.minor > host.minor)
        return "the plugin needs plugin API " + version_text(p) + ", newer than this Yantra's " +
               version_text(host) + "; update Yantra or use an older version of the plugin";
    if (!YANTRA_HAS_FIELD(d, yantra_plugin_descriptor, ui))
        return "the plugin descriptor is shorter than plugin API 1.0 allows (struct_size " +
               std::to_string(d->struct_size) + ")";
    return {};
}

/**
 * Copies `text` into a caller buffer by the PLG-03 rules: always
 * NUL-terminated; #YANTRA_ERR_BUFFER_TOO_SMALL (with as much as fits) when
 * it does not fit. `out_length` (may be NULL) gets the full length.
 */
inline yantra_status copy_text(std::string_view text, char* buffer, std::uint32_t buffer_size,
                               std::uint32_t* out_length = nullptr)
{
    if (out_length != nullptr)
        *out_length = static_cast<std::uint32_t>(text.size());
    if (buffer == nullptr || buffer_size == 0)
        return YANTRA_ERR_BUFFER_TOO_SMALL;
    const std::size_t n = text.size() < buffer_size ? text.size() : buffer_size - 1;
    if (n > 0)
        std::memcpy(buffer, text.data(), n);
    buffer[n] = '\0';
    return n == text.size() ? YANTRA_OK : YANTRA_ERR_BUFFER_TOO_SMALL;
}

/**
 * Runs `f` (returning ::yantra_status) and turns any exception into a
 * status, so none crosses the C boundary (PLG-03): std::bad_alloc gives
 * #YANTRA_ERR_NO_MEMORY, anything else #YANTRA_ERR_FAILED.
 */
template <typename F>
yantra_status guarded(F&& f) noexcept
{
    try {
        return f();
    } catch (const std::bad_alloc&) {
        return YANTRA_ERR_NO_MEMORY;
    } catch (...) {
        return YANTRA_ERR_FAILED;
    }
}

/** Short English name of a status code, for log lines. */
inline const char* status_name(yantra_status status) noexcept
{
    switch (status) {
    case YANTRA_OK: return "ok";
    case YANTRA_ERR_FAILED: return "failed";
    case YANTRA_ERR_INVALID_ARGUMENT: return "invalid argument";
    case YANTRA_ERR_UNSUPPORTED: return "unsupported";
    case YANTRA_ERR_NOT_FOUND: return "not found";
    case YANTRA_ERR_BUFFER_TOO_SMALL: return "buffer too small";
    case YANTRA_ERR_BUSY: return "busy";
    case YANTRA_ERR_VERSION: return "version not compatible";
    case YANTRA_ERR_NO_MEMORY: return "out of memory";
    case YANTRA_ERR_IO: return "I/O error";
    case YANTRA_ERR_SCOPE: return "outside the scope boundary";
    default: return "unknown status";
    }
}

} // namespace yantra::plugin_api

#endif // YANTRA_PLUGIN_HPP
