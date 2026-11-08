// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's Sender (share.h, EXP-07) through libcurl (spec 16.2):
//   - Http: an HTTP PUT of each file to <url>/<file name> (the name
//     URL-encoded), Content-Type by extension, redirects not followed,
//     the login (when given) as HTTP basic authentication;
//   - Smtp: one MIME e-mail (text and the files as attachments) to the
//     recipients through the configured server; STARTTLS is used when the
//     server offers it (smtp://) or the connection is TLS (smtps://), the
//     login (when given) as SMTP AUTH.
// The client name is download_client_name(); nothing else is sent. The
// tests use a loopback HTTP server and a loopback SMTP server only.
#pragma once

#include <memory>

#include "curl_downloader.h"
#include "yantra/app/share.h"

namespace yantra::app {

std::shared_ptr<Sender> make_curl_sender(CurlOptions options = {});

}  // namespace yantra::app
