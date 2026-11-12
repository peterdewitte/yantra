// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Radio menu and the radio windows (step 9.6; docs/APP.md "Radio
// windows"): Radios (selection, connect, disconnect), Settings, Debug,
// Calibration (with the NFR-07 reference noise) and Device info with the
// self-test (PLG-16, RAD-40..48, RF-02, HW-01b). Drawing only: the model
// is RadioWindows (app/model), built from the radio API's descriptors,
// the same code for every radio plugin.
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include <yantra/radio.h>

#include "gui.h"
#include "yantra/app/i18n.h"
#include "gui_widgets.h"
#include "yantra/core/calibration/table.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

const ImVec4 kOkColour{0.45F, 0.85F, 0.45F, 1.0F};
const ImVec4 kWarnColour{1.0F, 0.75F, 0.25F, 1.0F};

/// Gives a combo box its label in the UI tests' probe (ImGui reports none
/// for it): the "###" id, as for the other items.
void probe_label(ImGuiID id, const char* label) {
#ifdef IMGUI_ENABLE_TEST_ENGINE
    ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, 0);
#else
    (void)id;
    (void)label;
#endif
}

constexpr RadioWindow kWindows[] = {RadioWindow::List, RadioWindow::Settings, RadioWindow::Debug,
                                    RadioWindow::Calibration, RadioWindow::Info};

/// A button that is disabled with `reason` (DOC-07 c); true when pressed.
bool reason_button(const std::string& label, const char* tooltip, const std::string& reason) {
    ImGui::BeginDisabled(!reason.empty());
    const bool pressed = ImGui::Button(label.c_str());
    ImGui::EndDisabled();
    const bool shown = note_help(!reason.empty(), tooltip, reason.c_str());
    if (shown || (!help_shown_on_purpose() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
        ImGui::TextUnformatted(tooltip);
        if (!reason.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kBannerColour, "%s", reason.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    return pressed && reason.empty();
}

void wrapped(const std::string& text, const ImVec4* colour = nullptr) {
    if (text.empty()) return;
    ImGui::PushTextWrapPos(0.0F);
    if (colour != nullptr) ImGui::TextColored(*colour, "%s", text.c_str());
    else ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

/// The device line and message every radio window starts with; false
/// when there is no open device (the reason is shown).
bool draw_header(const DeviceHeader& h) {
    if (!h.connected) {
        wrapped(h.reason, &kBannerColour);
        if (!h.message.empty()) wrapped(h.message, h.message_error ? &kErrorColour : nullptr);
        return false;
    }
    ImGui::Text(tr("Radio: %s%s"), h.title.c_str(), h.busy ? tr("  (working...)") : "");
    if (!h.message.empty()) wrapped(h.message, h.message_error ? &kErrorColour : &kOkColour);
    ImGui::Separator();
    return true;
}

void row_text(const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    value_column(em(200.0F));
    ImGui::TextUnformatted(value.empty() ? "-" : value.c_str());
}

std::string number_text(double x, int decimals, const char* unit) {
    if (!std::isfinite(x)) return "unknown";
    char b[64];
    std::snprintf(b, sizeof b, "%.*f %s", decimals, x, unit);
    return b;
}

void sensors_table(const std::vector<SensorRow>& sensors, const char* id) {
    if (sensors.empty()) {
        ImGui::TextDisabled(tr("This radio has no sensors."));
        return;
    }
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    for (const SensorRow& s : sensors) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(s.label.c_str());
        if (!s.help.empty()) help_marker(s.help.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(s.value.empty() ? "-" : s.value.c_str());
    }
    ImGui::EndTable();
}

}  // namespace

// -- menu ---------------------------------------------------------------------------------------

void Gui::draw_radio_menu() {
    RadioWindows* rw = state_.radio_windows();
    if (!ImGui::BeginMenu(tr("Radio###menu.radio"), rw != nullptr)) {
        if (rw == nullptr) text_tooltip(tr("Open or create a project first: the radio windows work with its walk test."));
        return;
    }
    if (ImGui::IsWindowAppearing()) rw->load_plugins();  // PLG-16: the plugins before a search
    const RadioListView v = rw->list_view();
    if (ImGui::MenuItem(tr("Radios...###radio.list"))) (void)rw->open(RadioWindow::List);
    text_tooltip(tr("The radio plugins and their devices: choose, connect and disconnect a radio (PLG-16)."));
    if (ImGui::MenuItem(tr("Find radios###radio.find"))) (void)rw->find_radios();
    text_tooltip(tr("Ask every radio plugin which devices it sees now (RAD-30)."));
    ImGui::Separator();
    std::optional<std::size_t> chosen;
    for (const RadioRow& r : v.radios)
        if (r.chosen) chosen = r.index;
    const std::string reason = !v.searched      ? tr("Find the radios first.")
                               : !chosen        ? tr("Choose a radio first (Radio > Radios...).")
                                                : std::string();
    const struct {
        RadioWindow w;
        const char* label;
        const char* tip;
    } entries[] = {
        {RadioWindow::Settings, tr("Settings...###radio.settings"), tr("The chosen radio's settings, built from its descriptors (RAD-40..44).")},
        {RadioWindow::Debug, tr("Debug...###radio.debug"), tr("The radio's records, counters, events, sensors and log (RAD-14, RAD-24, RAD-25).")},
        {RadioWindow::Calibration, tr("Calibration...###radio.calibration"),
         tr("The level calibration table, the wizard, verify, frequency error and the reference noise (RF-02, NFR-07).")},
        {RadioWindow::Info, tr("Device info and self-test...###radio.info"), tr("The plugin, the device and its self-test (RAD-30, RAD-46).")},
    };
    for (const auto& e : entries) {
        if (ImGui::MenuItem(e.label, nullptr, false, reason.empty())) (void)rw->open(e.w, chosen);
        ActionView a;
        a.tooltip = e.tip;
        a.enabled = reason.empty();
        a.disabled_reason = reason;
        item_tooltip(a);
    }
    // PLG-16: Radio > <plugin> > <device> > window. Every loaded radio
    // plugin is listed; one without a usable device is greyed out with the
    // reason (not searched yet, no device found, in use, not loaded).
    if (!v.plugins.empty()) ImGui::Separator();
    for (const PluginRow& p : v.plugins) {
        const bool open = ImGui::BeginMenu((p.name + "###radio.p." + p.id).c_str(), p.usable);
        ActionView pa;
        pa.tooltip = tr("The devices of this radio plugin and their windows (PLG-16).");
        pa.enabled = p.usable;
        pa.disabled_reason = p.reason;
        item_tooltip(pa);
        if (!open) continue;
        for (const RadioRow& r : v.radios) {
            if (r.plugin_id != p.id) continue;
            const std::string id = "radio.d." + std::to_string(r.index);
            const bool device_open = ImGui::BeginMenu((r.label + "###" + id).c_str(), r.usable);
            ActionView da;
            da.tooltip = tr("This device's settings, debug, calibration and information windows; opening one chooses and "
                            "connects it.");
            da.enabled = r.usable;
            da.disabled_reason = r.reason;
            item_tooltip(da);
            if (!device_open) continue;
            const struct {
                RadioWindow w;
                const char* label;
                const char* suffix;
                const char* tip;
            } items[] = {{RadioWindow::Settings, tr("Settings..."), ".settings",
                          tr("The chosen radio's settings, built from its descriptors (RAD-40..44).")},
                         {RadioWindow::Debug, tr("Debug..."), ".debug",
                          tr("The radio's records, counters, events, sensors and log (RAD-14, RAD-24, RAD-25).")},
                         {RadioWindow::Calibration, tr("Calibration..."), ".calibration",
                          tr("The level calibration table, the wizard, verify, frequency error and the reference noise (RF-02, NFR-07).")},
                         {RadioWindow::Info, tr("Device info and self-test..."), ".info",
                          tr("The plugin, the device and its self-test (RAD-30, RAD-46).")}};
            for (const auto& e : items) {
                if (ImGui::MenuItem((std::string(e.label) + "###" + id + e.suffix).c_str())) (void)rw->open(e.w, r.index);
                text_tooltip(e.tip);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    draw_plugin_entries(YANTRA_MENU_RADIO);  // PLG-06: plugin.h plugins' entries
    ImGui::EndMenu();
}

// -- windows ------------------------------------------------------------------------------------------

void Gui::draw_radio_windows() {
    RadioWindows* rw = state_.radio_windows();
    if (rw == nullptr) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float offset = 0.0F;
    for (RadioWindow w : kWindows) {
        offset += 28.0F;
        if (!rw->is_open(w)) continue;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 80.0F + offset, vp->WorkPos.y + 40.0F + offset), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(em(640.0F), vp->WorkSize.x * 0.9F), std::max(std::min(em(360.0F), vp->WorkSize.y * 0.6F), std::min(em(780.0F), vp->WorkSize.y - 80.0F - offset))),
                                 ImGuiCond_FirstUseEver);
        bool open = true;
        const std::string name = std::string(radio_window_title(w)) + "###win." + radio_window_id(w);
        if (ImGui::Begin(name.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
            wrap_texts();
            help_button(w == RadioWindow::List       ? "2.1"
                        : w == RadioWindow::Settings ? "2.2"
                        : w == RadioWindow::Debug    ? "2.3"
                        : w == RadioWindow::Info     ? "2.4"
                                                     : "3.1");
            switch (w) {
            case RadioWindow::List: draw_radio_list(*rw); break;
            case RadioWindow::Settings: draw_radio_settings(*rw); break;
            case RadioWindow::Debug: draw_radio_debug(*rw); break;
            case RadioWindow::Calibration: draw_radio_calibration(*rw); break;
            case RadioWindow::Info: draw_radio_info(*rw); break;
            }
        }
        ImGui::End();
        if (!open) rw->close(w);
    }
    draw_radio_confirmation(*rw);
}

void Gui::draw_radio_confirmation(RadioWindows& rw) {
    const char* kId = tr("Confirm###dlg.radio.confirm");
    const std::optional<Confirmation>& c = rw.confirmation();
    if (c && !ImGui::IsPopupOpen(kId)) ImGui::OpenPopup(kId);
    if (!ImGui::BeginPopupModal(kId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    if (!c) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0F);
    ImGui::TextUnformatted(c->text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const char* ok = c->kind == Confirmation::Kind::Reset     ? tr("Reset###radio.confirm.ok")
                     : c->kind == Confirmation::Kind::Restart ? tr("Restart and apply###radio.confirm.ok")
                                                              : tr("Apply###radio.confirm.ok");
    if (dialog_button(ok, tr("Apply the change."))) {
        (void)rw.confirm();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###radio.confirm.cancel"), tr("Keep the current value.")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        rw.cancel_confirmation();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Gui::draw_radio_list(RadioWindows& rw) {
    const RadioListView v = rw.list_view();
    if (ImGui::Button(tr("Find radios###radio.list.find"))) (void)rw.find_radios();
    text_tooltip(tr("Ask every radio plugin which devices it sees now (RAD-30)."));
    ImGui::SameLine();
    ImGui::TextUnformatted(v.state.c_str());
    wrapped(v.note);
    const SettingsView h = rw.settings_view();
    if (!h.header.message.empty()) wrapped(h.header.message, h.header.message_error ? &kErrorColour : &kOkColour);
    ImGui::Separator();
    if (!v.searched) {
        wrapped(tr("Press Find radios: the radio plugins list the devices they see."), &kBannerColour);
    } else if (v.radios.empty()) {
        wrapped(tr("No radio found. The plugins below say why."), &kBannerColour);
    }
    for (const RadioRow& r : v.radios) {
        ImGui::BeginDisabled(!r.usable);
        std::string label = r.label + (r.connected ? (r.measuring ? tr("  (measuring)") : tr("  (connected)")) : "") +
                            "###radio.row." + std::to_string(r.index);
        if (ImGui::RadioButton(label.c_str(), r.chosen) && !r.chosen) (void)rw.choose(r.index);
        ImGui::EndDisabled();
        text_tooltip((r.plugin_id + " " + r.device_key + (r.simulated ? tr(" (simulated)") : "") +
                      (r.usable ? std::string() : "\n" + r.reason))
                         .c_str());
    }
    ImGui::Spacing();
    if (reason_button(tr("Connect###radio.list.connect"), tr("Open the chosen radio without measuring, for its windows."),
                      v.connect_reason))
        (void)rw.connect();
    ImGui::SameLine();
    if (reason_button(tr("Disconnect###radio.list.disconnect"), tr("Close the radio."), v.disconnect_reason)) (void)rw.disconnect();
    bool any_connected = false;
    for (const RadioRow& r : v.radios) any_connected = any_connected || r.connected;
    const std::string open_reason = any_connected ? std::string() : tr("Connect a radio first.");
    ImGui::SameLine();
    if (reason_button(tr("Settings###radio.list.settings"), tr("Open the settings window."), open_reason))
        (void)rw.open(RadioWindow::Settings);
    ImGui::SameLine();
    if (reason_button(tr("Debug###radio.list.debug"), tr("Open the debug window."), open_reason)) (void)rw.open(RadioWindow::Debug);
    ImGui::SameLine();
    if (reason_button(tr("Calibration###radio.list.calibration"), tr("Open the calibration window."), open_reason))
        (void)rw.open(RadioWindow::Calibration);
    ImGui::SameLine();
    if (reason_button(tr("Info###radio.list.info"), tr("Open the device info and self-test window."), open_reason))
        (void)rw.open(RadioWindow::Info);
    ImGui::Separator();
    if (section_header(tr("Radio plugins###radio.list.plugins"), tr("The radio plugins found, their state and version."))) {
        for (std::size_t i = 0; i < v.plugins.size(); ++i) {
            const PluginRow& p = v.plugins[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%s %s (%s)", p.name.c_str(), p.version.c_str(), p.status.c_str());
            text_tooltip((p.id + "\nAPI " + p.api + ", " + p.licence + "\n" + p.author + "\nCapabilities: " +
                          (p.capabilities.empty() ? std::string("none") : p.capabilities))
                             .c_str());
            if (!p.message.empty()) wrapped("  " + p.message, &kBannerColour);
            ImGui::PopID();
        }
    }
}

// -- settings -------------------------------------------------------------------------------------------

void Gui::draw_setting_row(RadioWindows& rw, const SettingRow& r) {
    const radio::SettingDef& d = r.def;
    const std::string id = "###set." + d.key;
    ImGui::PushID(d.key.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(d.label.c_str());
    std::string help = d.help;
    if (!d.unit.empty()) help += (help.empty() ? "" : "\n") + std::string(tr("Unit: ")) + d.unit;
    if (d.has(YR_SETTING_RESTART)) help += (help.empty() ? "" : "\n") + std::string(tr("Changes restart the stream."));
    if (d.has(YR_SETTING_CALIBRATION)) help += (help.empty() ? "" : "\n") + std::string(tr("Part of the calibration key (HW-01b)."));
    if (!help.empty()) help_marker(help.c_str());
    value_column(em(260.0F));
    ImGui::BeginDisabled(!r.enabled);
    const char* tip = r.enabled ? (d.help.empty() ? d.label.c_str() : d.help.c_str()) : r.disabled_reason.c_str();
    // The input field of a number or text, applied with Enter.
    auto input_field = [&](float width) {
        const std::string key = d.key;
        if (setting_editing_ != key) setting_text_[key] = setting_edit_text(d, r.requested);
        ImGui::SetNextItemWidth(width);
        std::string& text = setting_text_[key];
        const bool enter = input_text((id + ".input").c_str(), text, d.type == YR_TYPE_STRING ? YR_MAX_STRING_VALUE_BYTES + 1 : 64);
        text_tooltip(tip);
        if (ImGui::IsItemActive()) setting_editing_ = key;
        if (ImGui::IsItemDeactivatedAfterEdit() || (enter && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            std::string error;
            if (const auto v = parse_setting(d, text, error)) {
                setting_input_error_.erase(key);
                (void)rw.request(key, *v);
            } else {
                setting_input_error_[key] = error;
            }
            setting_editing_.clear();
        } else if (!ImGui::IsItemActive() && setting_editing_ == key) {
            setting_editing_.clear();
        }
    };
    switch (r.widget) {
    case SettingWidget::Checkbox: {
        bool on = std::holds_alternative<bool>(r.requested) && std::get<bool>(r.requested);
        if (ImGui::Checkbox(id.c_str(), &on)) (void)rw.request(d.key, on);
        text_tooltip(tip);
        break;
    }
    case SettingWidget::Combo: {
        // As wide as the longest choice, within the room left (TST-52).
        float widest = ImGui::CalcTextSize(r.requested_text.c_str()).x;
        for (const radio::EnumItem& it : d.items)
            widest = std::max(widest, ImGui::CalcTextSize((it.label.empty() ? it.name : it.label).c_str()).x);
        const float want = std::max(em(240.0F), widest + ImGui::GetStyle().FramePadding.x * 2.0F + ImGui::GetFrameHeight());
        // On a line of its own under the label when the room beside it is
        // too narrow for the longest choice.
        if (want > ImGui::GetContentRegionAvail().x) ImGui::NewLine();
        ImGui::SetNextItemWidth(std::min(want, ImGui::GetContentRegionAvail().x));
        const ImGuiID cid = ImGui::GetID(id.c_str());
        const bool open = ImGui::BeginCombo(id.c_str(), r.requested_text.c_str());
        probe_label(cid, id.c_str());
        if (open) {
            for (const radio::EnumItem& it : d.items) {
                const bool sel = std::holds_alternative<plugin::EnumValue>(r.requested) &&
                                 std::get<plugin::EnumValue>(r.requested).value == it.value;
                const std::string l = (it.label.empty() ? it.name : it.label) + "###set." + d.key + ".item." + it.name;
                if (ImGui::Selectable(l.c_str(), sel)) (void)rw.request(d.key, plugin::EnumValue{it.value});
            }
            ImGui::EndCombo();
        }
        text_tooltip(tip);
        break;
    }
    case SettingWidget::Slider: {
        ImGui::SetNextItemWidth(std::min(em(200.0F), ImGui::GetContentRegionAvail().x));
        bool done = false;
        plugin::Value v;
        if (d.type == YR_TYPE_DOUBLE) {
            double x = std::holds_alternative<double>(r.requested) ? std::get<double>(r.requested) : d.minimum;
            ImGui::SliderScalar(id.c_str(), ImGuiDataType_Double, &x, &d.minimum, &d.maximum, "%.3g");
            if (d.step > 0.0) x = d.minimum + std::round((x - d.minimum) / d.step) * d.step;
            v = x;
        } else if (d.type == YR_TYPE_INT) {
            std::int64_t x = std::holds_alternative<std::int64_t>(r.requested) ? std::get<std::int64_t>(r.requested)
                                                                                : static_cast<std::int64_t>(d.minimum);
            const auto lo = static_cast<std::int64_t>(d.minimum), hi = static_cast<std::int64_t>(d.maximum);
            ImGui::SliderScalar(id.c_str(), ImGuiDataType_S64, &x, &lo, &hi);
            v = x;
        } else {
            std::uint64_t x = std::holds_alternative<std::uint64_t>(r.requested) ? std::get<std::uint64_t>(r.requested)
                                                                                  : static_cast<std::uint64_t>(d.minimum);
            const auto lo = static_cast<std::uint64_t>(d.minimum), hi = static_cast<std::uint64_t>(d.maximum);
            ImGui::SliderScalar(id.c_str(), ImGuiDataType_U64, &x, &lo, &hi);
            v = x;
        }
        done = ImGui::IsItemDeactivatedAfterEdit();
        text_tooltip(tip);
        if (done) (void)rw.request(d.key, v);
        ImGui::SameLine();
        input_field(90.0F);
        break;
    }
    case SettingWidget::Number: input_field(160.0F); break;
    case SettingWidget::Text: input_field(240.0F); break;
    case SettingWidget::Button:
        if (ImGui::Button((d.label + id).c_str())) (void)rw.run_action(d.key);
        text_tooltip(tip);
        break;
    }
    if (!d.unit.empty() && r.widget != SettingWidget::Button && r.widget != SettingWidget::Checkbox) {
        ImGui::SameLine();
        ImGui::TextUnformatted(d.unit.c_str());
    }
    ImGui::EndDisabled();
    if (!r.enabled) {
        // After the control when it fits there, else below it, wrapped.
        const std::string reason = "(" + r.disabled_reason + ")";
        same_line_if(ImGui::CalcTextSize(reason.c_str()).x);
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", reason.c_str());
        ImGui::PopTextWrapPos();
    }
    if (r.differs) {
        ImGui::Indent(em(260.0F));
        ImGui::TextColored(kWarnColour, tr("Device uses: %s"), r.actual_text.c_str());
        text_tooltip(tr("The radio took another value than asked (RAD-41): it rounded or limited it."));
        ImGui::Unindent(em(260.0F));
    }
    const auto ie = setting_input_error_.find(d.key);
    const std::string& error = ie != setting_input_error_.end() ? ie->second : r.error;
    if (!error.empty()) {
        ImGui::Indent(em(260.0F));
        wrapped(error, &kErrorColour);
        ImGui::Unindent(em(260.0F));
    }
    if (!r.result.empty()) {
        ImGui::Indent(em(260.0F));
        wrapped(r.result);
        ImGui::Unindent(em(260.0F));
    }
    ImGui::PopID();
}

void Gui::draw_radio_settings(RadioWindows& rw) {
    const SettingsView v = rw.settings_view();
    if (!draw_header(v.header)) return;
    bool advanced = v.advanced;
    if (ImGui::Checkbox(tr("Advanced settings###set.advanced"), &advanced)) rw.set_advanced(advanced);
    text_tooltip(tr("Show the settings the radio marks as advanced."));
    if (!v.advanced && v.advanced_hidden != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled(tr("(%zu hidden)"), v.advanced_hidden);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset to defaults###set.reset"))) (void)rw.reset_defaults();
    text_tooltip(tr("Set every setting back to the radio's default (asks first)."));
    if (!v.presets.empty()) {
        ImGui::TextUnformatted(tr("Presets:"));
        for (const PresetRow& p : v.presets) {
            const std::string label = p.label + "###preset." + p.id;
            same_line_if(label_w(label.c_str()));
            if (ImGui::Button(label.c_str())) (void)rw.apply_preset(p.id);
            text_tooltip(p.help.empty() ? p.label.c_str() : p.help.c_str());
        }
    }
    ImGui::Separator();
    if (v.sections.empty()) ImGui::TextDisabled(v.header.busy ? tr("Reading the settings...") : tr("This radio has no settings."));
    ImGui::BeginChild("###set.list", ImVec2(0.0F, 0.0F));
    for (const SettingSection& s : v.sections) {
        const std::string title = (s.name.empty() ? std::string(tr("General")) : s.name) + "###set.group." + s.name;
        if (!section_header(title.c_str(), tr("A group of the radio's settings: click to show or hide it."), ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        for (const SettingRow& r : s.rows) draw_setting_row(rw, r);
    }
    ImGui::EndChild();
}

// -- debug ------------------------------------------------------------------------------------------------

void Gui::draw_radio_debug(RadioWindows& rw) {
    const DebugView v = rw.debug_view();
    if (!draw_header(v.header)) return;
    row_text(tr("Device state"), v.state);
    if (v.stats_valid) {
        const radio::DeviceStats& s = v.stats;
        row_text(tr("Records delivered"), std::to_string(s.sink.delivered));
        row_text(tr("Dropped (host)"), std::to_string(s.sink.dropped) + " dropped, " + std::to_string(s.sink.invalid) +
                                       " invalid, " + std::to_string(s.sink.late) + " late");
        row_text(tr("Streams / reopens"), std::to_string(s.streams) + " / " + std::to_string(s.reopens));
        if (s.plugin_ok) {
            row_text(tr("Plugin counters"), std::to_string(s.plugin.records_delivered) + " records, " +
                                            std::to_string(s.plugin.records_dropped) + " dropped, " +
                                            std::to_string(s.plugin.samples_dropped) + tr(" samples dropped, ") +
                                            std::to_string(s.plugin.overflows) + " overflows");
            row_text(tr("Receiver load"), number_text(s.plugin.receiver_load * 100.0, 0, "%"));
            row_text(tr("Sample rate"), number_text(s.plugin.sample_rate_hz / 1e6, 3, tr("MS/s")));
        }
        for (const std::string& p : s.sink.problems) wrapped(p, &kErrorColour);
    }
    if (section_header(tr("Records###dbg.records"), tr("The newest record of each carrier, as the radio delivers it."),
                       ImGuiTreeNodeFlags_DefaultOpen)) {
        if (v.records.empty()) ImGui::TextDisabled(tr("No records yet: start the radio (Survey > Start radio)."));
        for (std::size_t i = 0; i < v.records.size(); ++i) {
            const RecordRow& r = v.records[i];
            char head[200];
            std::snprintf(head, sizeof head, tr("%s %s  %.1f/s  %.1f s ago###dbg.rec.%zu"), r.carrier.c_str(), r.role.c_str(),
                          r.rate_hz, r.age_s, i);
            // A touch-sized row (UI-13) with its hover text (DOC-07).
            const bool open = ImGui::TreeNodeEx(head, layout().touch ? ImGuiTreeNodeFlags_FramePadding : 0);
            text_tooltip(tr("The newest record of this carrier: open it for its fields."));
            if (!open) continue;
            ImGui::TextDisabled(tr("%s  flags: %s"), r.schema.c_str(), r.flags.empty() ? "none" : r.flags.c_str());
            for (const auto& [name, value] : r.fields) row_text(name.c_str(), value);
            ImGui::TreePop();
        }
    }
    if (section_header(tr("Sensors###dbg.sensors"), tr("The readings of the radio's sensors."))) sensors_table(v.sensors, "###dbg.sensor_table");
    if (section_header(tr("Events###dbg.events"), tr("The radio's events, the newest first."))) {
        if (v.events.empty()) ImGui::TextDisabled(tr("No events yet."));
        for (auto it = v.events.rbegin(); it != v.events.rend(); ++it) ImGui::TextUnformatted(it->c_str());
    }
    if (section_header(tr("Plugin log###dbg.log"), tr("The messages of the radio plugin; warnings and errors in colour."))) {
        ImGui::BeginChild("###dbg.log_lines", ImVec2(0.0F, em(180.0F)), ImGuiChildFlags_Borders);
        for (const radio::LogLine& l : v.log) {
            const ImVec4* c = l.level >= YR_LOG_ERROR ? &kErrorColour : l.level >= YR_LOG_WARNING ? &kWarnColour : nullptr;
            if (c != nullptr) ImGui::TextColored(*c, "%s", l.message.c_str());
            else ImGui::TextUnformatted(l.message.c_str());
        }
        ImGui::EndChild();
    }
    if (!v.context_calls.empty() && section_header(tr("Context calls###dbg.context"), tr("How often the plugin called each function of Yantra."))) {
        for (const auto& [name, n] : v.context_calls) row_text(name.c_str(), std::to_string(n));
    }
}

// -- calibration -------------------------------------------------------------------------------------------

void Gui::draw_radio_calibration(RadioWindows& rw) {
    const CalibrationView v = rw.calibration_view();
    CalibrationForm& f = rw.calibration_form();
    if (!draw_header(v.header)) return;
    row_text(tr("Calibration key"), v.key_text);
    row_text(tr("Status"), v.key_status);
    row_text(tr("Table"), v.table_file);
    if (v.simulated) wrapped(tr("Simulated radio: practice calibration, never applied to a real radio."), &kWarnColour);

    if (section_header(tr("Entries of this device###cal.entries"), tr("The calibration table's entries for this device."),
                       ImGuiTreeNodeFlags_DefaultOpen)) {
        if (v.entries.empty()) ImGui::TextDisabled(tr("No calibration of this device yet."));
        for (std::size_t i = 0; i < v.entries.size(); ++i) {
            const CalibrationEntryRow& e = v.entries[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text(tr("%s%s  %s, %zu points, %s until %s%s"), e.current ? "> " : "  ", e.date.c_str(), e.method.c_str(),
                        e.points, e.status.c_str(), e.expires.c_str(), e.simulated ? tr(" (simulated)") : "");
            text_tooltip((e.key_id + "\nSerial " + e.serial + "\nSettings: " + e.settings).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton((tr("Remove###cal.remove.") + std::to_string(i)).c_str())) (void)rw.remove_entry(e.key_id);
            text_tooltip(tr("Remove this entry from the calibration table."));
            ImGui::PopID();
        }
    }

    if (section_header(tr("Level calibration wizard###cal.wizard"),
                       tr("Calibrate the level step by step with a signal generator or a reference radio."),
                       ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(v.wizard);
        ImGui::SetNextItemWidth(std::min(em(260.0F), ImGui::GetContentRegionAvail().x));
        input_text(tr("Frequencies (MHz)###cal.freqs"), f.frequencies_mhz);
        text_tooltip(tr("The generator's frequencies, comma separated."));
        ImGui::SetNextItemWidth(std::min(em(260.0F), ImGui::GetContentRegionAvail().x));
        input_text(tr("Levels (dBm)###cal.levels"), f.levels_dbm);
        text_tooltip(tr("The generator's levels at the radio's input, comma separated."));
        ImGui::SetNextItemWidth(std::min(em(120.0F), ImGui::GetContentRegionAvail().x));
        ImGui::InputInt(tr("Valid (days)###cal.days"), &f.valid_days);
        text_tooltip(tr("How long the calibration stays valid (RF-02)."));
        int readings = static_cast<int>(f.readings);
        ImGui::SetNextItemWidth(std::min(em(120.0F), ImGui::GetContentRegionAvail().x));
        if (ImGui::InputInt(tr("Readings per point###cal.readings"), &readings)) f.readings = static_cast<std::size_t>(std::max(1, readings));
        text_tooltip(tr("Records averaged per point."));
        const bool reference = f.method == calibration::kMethodReference;
        if (ImGui::RadioButton(tr("Signal generator###cal.method.generator"), !reference)) f.method = calibration::kMethodGenerator;
        text_tooltip(tr("Known levels from a signal generator at the radio's input."));
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Reference radio###cal.method.reference"), reference)) f.method = calibration::kMethodReference;
        text_tooltip(tr("Levels measured at the same time by a calibrated reference radio."));
        ImGui::EndDisabled();
        if (reason_button(tr("Start###cal.start"), tr("Start the calibration with these points."), v.start_reason)) (void)rw.start_calibration();
        if (v.wizard) {
            wrapped(v.target);
            if (reason_button(tr("Measure###cal.measure"), tr("Average the radio's records at this point."), v.measure_reason))
                (void)rw.measure_point();
            ImGui::SameLine();
            if (ImGui::Button(tr("Skip###cal.skip"))) (void)rw.skip_point();
            text_tooltip(tr("Leave this point out."));
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel###cal.cancel"))) (void)rw.cancel_calibration();
            text_tooltip(tr("Stop the calibration; nothing is stored."));
            ImGui::SameLine();
            if (reason_button(tr("Save###cal.save"), tr("Store the result in the calibration table."),
                              v.result && !v.result->points.empty() ? std::string() : tr("No accepted points yet.")))
                (void)rw.save_calibration();
            if (v.collecting.active)
                ImGui::ProgressBar(v.collecting.want == 0 ? 0.0F : static_cast<float>(v.collecting.have) / v.collecting.want,
                                   ImVec2(em(260.0F), 0.0F));
            for (const calibration::Reading& r : v.readings) {
                if (r.accepted)
                    ImGui::Text(tr("%.4f MHz %.1f dBm: %.2f dBFS"), r.frequency_hz / 1e6, r.level_dbm, r.measured_dbfs);
                else
                    ImGui::TextColored(kErrorColour, "%.4f MHz %.1f dBm: %s", r.frequency_hz / 1e6, r.level_dbm, r.problem.c_str());
            }
        }
    }

    if (section_header(tr("Verify and frequency error###cal.verify_section"),
                       tr("Check the calibration at a known level and measure the frequency error."))) {
        ImGui::SetNextItemWidth(std::min(em(140.0F), ImGui::GetContentRegionAvail().x));
        ImGui::InputDouble(tr("MHz###cal.verify_mhz"), &f.verify_mhz, 0.0, 0.0, "%.4f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(em(100.0F), ImGui::GetContentRegionAvail().x));
        ImGui::InputDouble(tr("dBm###cal.verify_dbm"), &f.verify_dbm, 0.0, 0.0, "%.1f");
        ImGui::SameLine();
        if (reason_button(tr("Verify###cal.verify"), tr("Measure a known level and compare with the table."), v.verify_reason))
            (void)rw.verify();
        if (v.verify_error_db) ImGui::Text(tr("Table error: %+.2f dB"), *v.verify_error_db);
        ImGui::SetNextItemWidth(std::min(em(140.0F), ImGui::GetContentRegionAvail().x));
        ImGui::InputDouble(tr("Known carrier (MHz)###cal.carrier_mhz"), &f.carrier_mhz, 0.0, 0.0, "%.4f");
        ImGui::SameLine();
        if (reason_button(tr("Measure ppm###cal.ppm"),
                          tr("The radio's frequency error from a carrier of known frequency (its records' frequency error)."),
                          v.ppm_reason))
            (void)rw.measure_ppm();
        if (v.ppm) ImGui::Text(tr("Frequency error: %+.3f ppm"), *v.ppm);
    }

    if (section_header(tr("Reference noise (NFR-07)###cal.noise"), tr("The noise reference of the self-interference warning."),
                       ImGuiTreeNodeFlags_DefaultOpen)) {
        wrapped(tr("Take the reference with the antenna away from the PC: the self-interference warning compares the "
                "measured noise with it (else with the thermal floor)."));
        row_text(tr("Thermal floor"), number_text(v.thermal_dbm, 1, "dBm"));
        row_text(tr("Reference"), v.noise_reference_dbm ? number_text(*v.noise_reference_dbm, 1, "dBm") : std::string(tr("not taken")));
        row_text(tr("Noise now"), v.last_noise_dbm ? number_text(*v.last_noise_dbm, 1, "dBm") : std::string("unknown"));
        if (reason_button(tr("Take reference###cal.noise.take"), tr("Median of the noise over 5 cycles."), v.noise_reason))
            (void)rw.take_noise_reference();
        ImGui::SameLine();
        if (reason_button(tr("Clear###cal.noise.clear"), tr("Use the thermal floor again."),
                          v.noise_reference_dbm ? std::string() : tr("No reference taken.")))
            (void)rw.clear_noise_reference();
        if (v.collecting.active && !v.wizard)
            ImGui::ProgressBar(v.collecting.want == 0 ? 0.0F : static_cast<float>(v.collecting.have) / v.collecting.want,
                               ImVec2(em(260.0F), 0.0F));
    }

    if (section_header(tr("Import and export###cal.files"), tr("Read or write the calibration table as a file."))) {
        ImGui::SetNextItemWidth(std::min(em(360.0F), ImGui::GetContentRegionAvail().x));
        input_text(tr("File###cal.file"), f.file);
        text_tooltip(tr("A calibration table file (JSON)."));
        const std::string why = f.file.empty() ? tr("Enter a file name.") : std::string();
        if (reason_button(tr("Import###cal.import"), tr("Merge the file's entries (the newer entry wins)."), why))
            (void)rw.import_table(f.file);
        ImGui::SameLine();
        if (reason_button(tr("Export###cal.export"), tr("Write the whole calibration table to the file."), why))
            (void)rw.export_table(f.file);
    }
}

// -- device info -----------------------------------------------------------------------------------------------

std::string Gui::shown_plugin_file(const std::filesystem::path& path, bool screenshot_mode) {
    // A screenshot names the file only: the folder is the machine's (it can
    // hold a home folder), and the documentation must not show it.
    return screenshot_mode ? "<plugins>/" + path.filename().string() : path.string();
}

void Gui::draw_radio_info(RadioWindows& rw) {
    const InfoView v = rw.info_view();
    if (!draw_header(v.header)) return;
    // The self-test first: the sections below change size as sensors come.
    ImGui::SeparatorText(tr("Self-test"));
    if (reason_button(tr("Run self-test###info.self_test"), tr("Check the device (RAD-46)."), v.self_test_reason)) (void)rw.run_self_test();
    if (v.self_test_ran && v.self_test.empty()) ImGui::TextDisabled(tr("The radio reported no test items."));
    if (v.self_test_ran && !v.self_test.empty() &&
        ImGui::BeginTable("###info.self_test_table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const radio::SelfTestItem& i : v.self_test) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const ImVec4& c = i.result == YR_TEST_FAIL ? kErrorColour : i.result == YR_TEST_WARN ? kWarnColour : kOkColour;
            ImGui::TextColored(c, "%s", radio::test_result_text(i.result));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(i.label.c_str());
            ImGui::TableNextColumn();
            wrapped(i.detail);
            if (!i.remedy.empty()) wrapped(i.remedy, &kWarnColour);
        }
        ImGui::EndTable();
    }
    if (v.plugin_known) {
        ImGui::SeparatorText(tr("Plugin"));
        row_text(tr("Name"), v.plugin.name);
        row_text(tr("Id"), v.plugin.id);
        row_text(tr("Version"), v.plugin.version);
        row_text(tr("API"), std::to_string(v.plugin.api_version.major) + "." + std::to_string(v.plugin.api_version.minor));
        row_text(tr("Author"), v.plugin.author);
        row_text(tr("Licence"), v.plugin.licence);
        row_text(tr("Source"), v.plugin.source_url);
        row_text(tr("Capabilities"), radio::capabilities_text(v.plugin.capabilities));
        row_text(tr("File"), shown_plugin_file(v.plugin.path, screenshot_mode_));
    }
    if (v.device) {
        ImGui::SeparatorText(tr("Device"));
        row_text(tr("Label"), v.device->label);
        row_text(tr("Key"), v.device->key);
        row_text(tr("Driver"), v.device->driver);
        row_text(tr("Serial"), v.device->serial);
        row_text(tr("Module"), v.device->module_version);
        row_text(tr("Hardware"), v.device->hardware_info);
        row_text(tr("Flags"), v.device_flags + (v.device->flags_reason.empty() ? "" : ": " + v.device->flags_reason));
    }
    row_text(tr("Calibration key"), v.calibration_key);
    ImGui::SeparatorText(tr("Sensors"));
    sensors_table(v.sensors, "###info.sensor_table");
}

}  // namespace yantra::app
