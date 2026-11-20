// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host-side helpers for the plugin C ABI (plugin-api/include/yantra/plugin.h): the
// API version check with a clear message (PLG-04) and safe reading of
// versioned structs filled by plugins built for an older or newer minor
// version (struct_size, PLG-03). No app or UI dependency.
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plugin/abi.h,
// adapted for plugin API 1.0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <yantra/plugin.h>

namespace yantra::plugin {

// Plugin API version of the plugin.h this host was built with.
yantra_api_version host_api_version() noexcept;

enum class ApiCompat {
    Compatible,     // same major, plugin minor <= host minor
    MajorMismatch,  // built for another major version
    MinorTooNew,    // built for a newer minor version than the host
};

struct ApiCheck {
    ApiCompat result = ApiCompat::Compatible;
    std::string message;  // empty when compatible; otherwise for the user
    bool ok() const noexcept { return result == ApiCompat::Compatible; }
};

// Applies the PLG-04 rule and explains a refusal, naming the plugin.
ApiCheck check_api_version(yantra_api_version host, yantra_api_version plugin,
                           const std::string& plugin_name);

// Copies a versioned struct filled by the other side. Reads only the bytes
// inside both struct_size and sizeof(T): fields the other side does not
// know (older minor) come back zero; fields this host does not know (newer
// minor) are not read. Returns nullopt for NULL or a struct_size below
// min_size (the struct's API 1.0 size; all structs are 1.0 structs now).
template <class T>
std::optional<T> read_versioned(const T* p, std::size_t min_size = sizeof(T)) {
    static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
    static_assert(offsetof(T, struct_size) == 0);
    if (p == nullptr) {
        return std::nullopt;
    }
    std::uint32_t declared = 0;
    std::memcpy(&declared, p, sizeof declared);
    if (declared < min_size) {
        return std::nullopt;
    }
    T copy{};
    const std::size_t n = declared < sizeof(T) ? declared : sizeof(T);
    std::memcpy(static_cast<void*>(&copy), static_cast<const void*>(p), n);
    return copy;
}

// Result of turning plugin-provided C data into checked C++ objects: the
// value when valid, else the list of problems (for the log and the plugin
// manager, PLG-09).
template <class T>
struct Parsed {
    std::optional<T> value;
    std::vector<std::string> errors;
    bool ok() const noexcept { return value.has_value(); }
};

// Text of a yantra_status code, for messages.
const char* status_name(yantra_status status) noexcept;

// NULL-safe copy of a C string.
inline std::string text_of(const char* s) {
    return s == nullptr ? std::string{} : std::string{s};
}

}  // namespace yantra::plugin
