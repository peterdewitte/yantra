// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// EXP-07 data sharing: an export or report is sent after it is written,
// by one of the methods of SharingSettings (settings.h; docs/APP.md
// "Export, report and sharing"):
//   - Folder: the files are copied into a shared folder (a mounted network
//     share), each through a temporary name and then renamed, so a reader
//     never sees half a file;
//   - Http: each file is uploaded with an HTTP PUT to <url>/<file name>;
//   - Smtp: one e-mail with the files attached, through the team's own
//     mail server.
// Nothing is sent unless the user chose a method; only the files, the
// addresses and the login the user typed are sent (no other data). The
// network methods go through a Sender (app/net/curl_sender.h: libcurl,
// spec 16.2); the model sees this interface only, so tests give their
// own or a local fake server.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "yantra/app/settings.h"

namespace yantra::app {

struct ShareJob {
    SharingSettings settings;
    std::string password;  ///< of settings.user; typed, never stored
    std::vector<std::filesystem::path> files;
    std::string subject;  ///< the e-mail's subject (Smtp)
    std::string text;     ///< the e-mail's text (Smtp)
};

/// Sends files over the network (Http, Smtp). "" on success, else the
/// reason in one line. Blocks (the app runs it on a worker thread).
class Sender {
public:
    virtual ~Sender() = default;
    virtual std::string send(const ShareJob& job) = 0;
};

/// A sender that always fails with `reason` (a copy of Yantra without
/// network sending; the default of AppState).
std::shared_ptr<Sender> make_no_sender(std::string reason);

/// Why `s` cannot send ("" when it can): a folder for Folder, an http(s)
/// URL for Http, an smtp(s) server and the addresses for Smtp; Off
/// cannot send.
std::string check_sharing(const SharingSettings& s);

/// The addresses of SharingSettings::mail_to, trimmed.
std::vector<std::string> mail_recipients(const std::string& mail_to);

/// Copies the files into `folder` (Folder method): <name>.part, then
/// renamed over an older file of that name. "" or the reason.
std::string copy_to_folder(const std::vector<std::filesystem::path>& files, const std::filesystem::path& folder);

/// Sends a job by its method: Folder copies, Http and Smtp go through
/// `network` (nullptr: refused with the reason). "" or the reason.
std::string share_files(const ShareJob& job, Sender* network);

}  // namespace yantra::app
