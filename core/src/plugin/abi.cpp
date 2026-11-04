// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Copied from yantra-v2 68ea20a: core/src/plugin/abi.cpp, adapted.

#include "yantra/core/plugin/abi.h"

#include <yantra/plugin.hpp>

namespace yantra::plugin {

yantra_api_version host_api_version() noexcept {
    return yantra_api_version_compiled();
}

ApiCheck check_api_version(yantra_api_version host, yantra_api_version plugin,
                           const std::string& plugin_name) {
    ApiCheck check;
    if (yantra_api_compatible(host, plugin)) {
        return check;
    }
    const std::string head = "plugin '" + plugin_name + "' was built for plugin API " +
                             plugin_api::version_text(plugin) +
                             ", but this Yantra supports plugin API " +
                             plugin_api::version_text(host);
    if (plugin.major != host.major) {
        check.result = ApiCompat::MajorMismatch;
        check.message = head + " (different major version); it needs a version of the "
                               "plugin built for plugin API " +
                        std::to_string(host.major) + ".x";
    } else {
        check.result = ApiCompat::MinorTooNew;
        check.message = head + " (newer minor version); update Yantra or use an older "
                               "version of the plugin";
    }
    return check;
}

const char* status_name(yantra_status status) noexcept {
    return plugin_api::status_name(status);
}

}  // namespace yantra::plugin
