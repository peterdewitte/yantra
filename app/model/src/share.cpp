// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// EXP-07 sharing, see share.h.
#include "yantra/app/share.h"

#include <system_error>

#include "yantra/app/i18n.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

class NoSender final : public Sender {
public:
    explicit NoSender(std::string reason) : reason_(std::move(reason)) {}
    std::string send(const ShareJob&) override { return reason_; }

private:
    std::string reason_;
};

bool starts_with(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

std::shared_ptr<Sender> make_no_sender(std::string reason) { return std::make_shared<NoSender>(std::move(reason)); }

std::vector<std::string> mail_recipients(const std::string& mail_to) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= mail_to.size()) {
        const std::size_t comma = mail_to.find_first_of(",;", pos);
        const std::string a = trimmed(mail_to.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
        if (!a.empty()) out.push_back(a);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

std::string check_sharing(const SharingSettings& s) {
    auto address = [](const std::string& a) {
        const auto at = a.find('@');
        return at != std::string::npos && at > 0 && at + 1 < a.size() && a.find_first_of(" <>\r\n") == std::string::npos;
    };
    switch (s.method) {
    case SharingSettings::Method::Off: return tr("Sharing is off: choose a folder, an upload or e-mail first.");
    case SharingSettings::Method::Folder:
        if (s.folder.empty()) return tr("Choose the shared folder the files are copied to.");
        return {};
    case SharingSettings::Method::Http:
        if (!starts_with(s.url, "http://") && !starts_with(s.url, "https://"))
            return tr("The upload address starts with https:// (or http://).");
        return {};
    case SharingSettings::Method::Smtp: {
        if (!starts_with(s.smtp_server, "smtp://") && !starts_with(s.smtp_server, "smtps://"))
            return tr("The mail server starts with smtp:// or smtps://, e.g. smtp://mail.example.org:587.");
        if (!address(s.mail_from)) return tr("Give the sender's e-mail address.");
        const auto to = mail_recipients(s.mail_to);
        if (to.empty()) return tr("Give at least one recipient's e-mail address.");
        for (const std::string& a : to)
            if (!address(a)) return trf("\"{}\" is not an e-mail address.", {a});
        return {};
    }
    }
    return {};
}

std::string copy_to_folder(const std::vector<fs::path>& files, const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return trf("The shared folder {} cannot be found.", {folder.string()});
    for (const fs::path& f : files) {
        const fs::path target = folder / f.filename();
        fs::path part = target;
        part += ".part";
        fs::copy_file(f, part, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::rename(part, target, ec);
        if (ec) {
            fs::remove(part, ec);
            return trf("{} cannot be copied to {}.", {f.filename().string(), folder.string()});
        }
    }
    return {};
}

std::string share_files(const ShareJob& job, Sender* network) {
    if (std::string why = check_sharing(job.settings); !why.empty()) return why;
    if (job.files.empty()) return tr("There is nothing to send.");
    std::error_code ec;
    for (const fs::path& f : job.files)
        if (!fs::is_regular_file(f, ec)) return trf("{} cannot be found.", {f.string()});
    if (job.settings.method == SharingSettings::Method::Folder) return copy_to_folder(job.files, job.settings.folder);
    if (network == nullptr) return tr("This copy of Yantra cannot send over the network.");
    return network->send(job);
}

}  // namespace yantra::app
