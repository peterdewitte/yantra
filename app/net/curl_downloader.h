// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's Downloader (sheet_fetch.h) through libcurl (spec 16.2: the
// MAK download of DEMO-12): one plain GET with the generic client name
// download_client_name() and nothing else (no cookies, no referrer, no
// extra headers). https is used for the descriptions' URLs; file:// and
// http:// are accepted as well (the tests use a local file and a loopback
// server: no network). Redirects are followed to https only, at most 5.
#pragma once

#include <memory>

#include "yantra/app/sheet_fetch.h"

namespace yantra::app {

struct CurlOptions {
    long connect_timeout_s = 20;
    /// The transfer is given up when slower than 1 byte/s for this long.
    long stall_timeout_s = 60;
};

std::shared_ptr<Downloader> make_curl_downloader(CurlOptions options = {});

}  // namespace yantra::app
