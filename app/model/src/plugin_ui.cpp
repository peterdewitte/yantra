// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plugins' own UI (PLG-06), see plugin_ui.h.
#include "yantra/app/plugin_ui.h"

#include <algorithm>

#include "yantra/core/plugin/loader.h"

namespace yantra::app {

const char* plugin_menu_key(std::uint32_t menu) {
    switch (menu) {
    case YANTRA_MENU_FILE: return "file";
    case YANTRA_MENU_VIEW: return "view";
    case YANTRA_MENU_RADIO: return "radio";
    case YANTRA_MENU_TOOLS: return "tools";
    case YANTRA_MENU_HELP: return "help";
    default: return "tools";
    }
}

PluginUi::PluginUi(plugin::PluginLoader& loader) : loader_(loader) { tick(); }

bool PluginUi::enabled(const std::string& plugin_id) const {
    const auto e = loader_.entry(plugin_id);
    return e && e->status == plugin::PluginStatus::Enabled && loader_.info(plugin_id) != nullptr;
}

void PluginUi::tick() {
    // Plugins switched off (or failed): their windows go.
    std::vector<std::string> now;
    for (const plugin::PluginEntry& e : loader_.list())
        if (e.status == plugin::PluginStatus::Enabled) now.push_back(e.id);
    open_.erase(std::remove_if(open_.begin(), open_.end(),
                               [&](const PluginWindowView& w) {
                                   return std::find(now.begin(), now.end(), w.plugin_id) == now.end();
                               }),
                open_.end());
    // Newly enabled plugins open their OPEN_AT_START windows.
    for (const std::string& id : now) {
        if (std::find(enabled_.begin(), enabled_.end(), id) != enabled_.end()) continue;
        if (const plugin::PluginInfo* info = loader_.info(id))
            for (const plugin::UiWindow& w : info->ui.windows)
                if (w.open_at_start) (void)open(id, w.id);
    }
    enabled_ = std::move(now);
    for (const plugin::UiRequest& r : loader_.take_ui_requests()) {
        if (r.open) (void)open(r.plugin_id, r.window_id);
        else close(r.plugin_id, r.window_id);
    }
}

std::vector<PluginMenuEntry> PluginUi::menu(std::uint32_t menu) const {
    std::vector<PluginMenuEntry> out;
    for (const plugin::PluginEntry& e : loader_.list()) {
        if (e.status != plugin::PluginStatus::Enabled) continue;
        const plugin::PluginInfo* info = loader_.info(e.id);
        const yantra_plugin_descriptor* d = loader_.descriptor(e.id);
        if (info == nullptr || d == nullptr) continue;
        for (const plugin::UiMenuItem& m : info->ui.menu_items) {
            if (m.menu != menu) continue;
            PluginMenuEntry x;
            x.plugin_id = e.id;
            x.item_id = m.id;
            x.id = "plg." + e.id + "." + m.id;
            x.menu = m.menu;
            x.submenu = m.submenu.empty() ? "" : translate(e.id, m.submenu);
            x.label = translate(e.id, m.label);
            x.tooltip = translate(e.id, m.tooltip);
            x.shortcut = m.shortcut;
            x.window_id = m.window_id;
            x.checked = (m.flags & YANTRA_UI_CHECKED) != 0;
            if (d->ui != nullptr && d->ui->menu_state != nullptr) {
                yantra_ui_menu_state st{};
                st.struct_size = sizeof st;
                if (d->ui->menu_state(loader_.instance(e.id), m.id.c_str(), &st) == YANTRA_OK) {
                    x.checked = (st.flags & YANTRA_UI_CHECKED) != 0;
                    if ((st.flags & YANTRA_UI_DISABLED) != 0) {
                        x.enabled = false;
                        x.disabled_reason = st.disabled_reason != nullptr && *st.disabled_reason != '\0'
                                                ? translate(e.id, st.disabled_reason)
                                                : std::string("Not available now.");
                    }
                }
            }
            out.push_back(std::move(x));
        }
    }
    return out;
}

bool PluginUi::has_menu(std::uint32_t menu) const {
    for (const plugin::PluginEntry& e : loader_.list()) {
        if (e.status != plugin::PluginStatus::Enabled) continue;
        if (const plugin::PluginInfo* info = loader_.info(e.id))
            for (const plugin::UiMenuItem& m : info->ui.menu_items)
                if (m.menu == menu) return true;
    }
    return false;
}

Result PluginUi::select(const std::string& plugin_id, const std::string& item_id) {
    if (!enabled(plugin_id)) return Result{false, "The plugin is not enabled."};
    const plugin::PluginInfo* info = loader_.info(plugin_id);
    const auto it = std::find_if(info->ui.menu_items.begin(), info->ui.menu_items.end(),
                                 [&](const plugin::UiMenuItem& m) { return m.id == item_id; });
    if (it == info->ui.menu_items.end()) return Result{false, "The plugin has no menu entry " + item_id + "."};
    if (!it->window_id.empty()) return open(plugin_id, it->window_id);
    const yantra_plugin_descriptor* d = loader_.descriptor(plugin_id);
    if (d == nullptr || d->ui == nullptr || d->ui->menu_selected == nullptr)
        return Result{false, "The plugin does not handle this menu entry."};
    d->ui->menu_selected(loader_.instance(plugin_id), item_id.c_str());
    return Result{true, ""};
}

Result PluginUi::open(const std::string& plugin_id, const std::string& window_id) {
    if (!enabled(plugin_id)) return Result{false, "The plugin is not enabled."};
    if (is_open(plugin_id, window_id)) return Result{true, ""};
    const plugin::PluginInfo* info = loader_.info(plugin_id);
    const auto it = std::find_if(info->ui.windows.begin(), info->ui.windows.end(),
                                 [&](const plugin::UiWindow& w) { return w.id == window_id; });
    if (it == info->ui.windows.end()) return Result{false, "The plugin has no window " + window_id + "."};
    PluginWindowView v;
    v.plugin_id = plugin_id;
    v.window_id = window_id;
    v.id = "plg." + plugin_id + "." + window_id;
    v.kind = it->kind;
    v.title = translate(plugin_id, it->title);
    v.help = translate(plugin_id, it->help);
    v.width = it->default_width;
    v.height = it->default_height;
    open_.push_back(std::move(v));
    return Result{true, ""};
}

void PluginUi::close(const std::string& plugin_id, const std::string& window_id) {
    const auto it = std::find_if(open_.begin(), open_.end(), [&](const PluginWindowView& w) {
        return w.plugin_id == plugin_id && w.window_id == window_id;
    });
    if (it == open_.end()) return;
    open_.erase(it);
    if (!enabled(plugin_id)) return;
    const yantra_plugin_descriptor* d = loader_.descriptor(plugin_id);
    if (d != nullptr && d->ui != nullptr && d->ui->window_closed != nullptr)
        d->ui->window_closed(loader_.instance(plugin_id), window_id.c_str());
}

bool PluginUi::is_open(const std::string& plugin_id, const std::string& window_id) const {
    return std::any_of(open_.begin(), open_.end(), [&](const PluginWindowView& w) {
        return w.plugin_id == plugin_id && w.window_id == window_id;
    });
}

void PluginUi::draw(const PluginWindowView& window, const yantra_ui_api& api) const {
    if (!enabled(window.plugin_id)) return;
    const yantra_plugin_descriptor* d = loader_.descriptor(window.plugin_id);
    if (d == nullptr || d->ui == nullptr || d->ui->draw == nullptr) return;
    d->ui->draw(loader_.instance(window.plugin_id), window.window_id.c_str(), &api);
}

std::string PluginUi::translate(const std::string& plugin_id, const std::string& english) const {
    if (language_ == "en" || language_.empty()) return english;
    const plugin::PluginInfo* info = loader_.info(plugin_id);
    if (info == nullptr) return english;
    for (const plugin::Translation& t : info->translations)
        if (t.key == english && t.lang == language_ && !t.text.empty()) return t.text;
    return english;
}

void PluginUi::set_language(std::string language) {
    language_ = std::move(language);
    for (PluginWindowView& w : open_) {
        if (const plugin::PluginInfo* info = loader_.info(w.plugin_id))
            for (const plugin::UiWindow& x : info->ui.windows)
                if (x.id == w.window_id) {
                    w.title = translate(w.plugin_id, x.title);
                    w.help = translate(w.plugin_id, x.help);
                }
    }
}

}  // namespace yantra::app
