// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The checks of the GUI text-overflow test, see text_lint.h.
#include "text_lint.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

#include "plan_fixtures.h"
#include "yantra/core/plan/source.h"

namespace yantra::test {

namespace {

constexpr float kTol = 1.0F;  // pixels: rounding of glyph advances

std::string_view key_of(std::string_view label) {
    const auto p = label.find("###");
    return p == std::string_view::npos ? label : label.substr(p + 3);
}

std::string px(float v) { return std::to_string(static_cast<int>(std::ceil(v - 0.01F))); }

std::string shorten(const std::string& s) {
    std::string t = s.substr(0, s.find('\n'));
    if (t.size() > 60) { std::size_t n = 57; while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) --n; t = t.substr(0, n) + "..."; }
    return "\"" + t + "\"";
}

// A control of the probe (as the UI lint's: a stable "###" id, not a
// window, child region or menu-bar title).
bool is_control(const app::ProbeItem& it) {
    if (it.label.find("###") == std::string::npos || it.label.rfind("###", 0) == 0) return false;
    const std::string_view key = it.key();
    if (key.find('/') != std::string_view::npos || key == key_of(it.window)) return false;
    if (it.window == "##MainMenuBar") return false;
    for (std::string_view n : {"plan.canvas", "status_bar", "help.text"})
        if (key == n) return false;
    return true;
}

ImRect overlap(const ImRect& a, const ImRect& b) {
    return ImRect(ImMax(a.Min, b.Min), ImMin(a.Max, b.Max));
}

// The text runs past `edge` on one axis: allowed when that edge is the
// clip edge of a window (or a parent) that scrolls on that axis.
bool scrolls_there(const ImGuiWindow* w, bool x_axis, float edge) {
    for (; w != nullptr; w = w->ParentWindow) {
        const ImRect& c = w->InnerClipRect;
        const bool at = x_axis ? (std::abs(c.Min.x - edge) <= 1.0F || std::abs(c.Max.x - edge) <= 1.0F)
                               : (std::abs(c.Min.y - edge) <= 1.0F || std::abs(c.Max.y - edge) <= 1.0F);
        if (at && (x_axis ? w->ScrollbarX : w->ScrollbarY)) return true;
    }
    return false;
}

const char* clipper(const DrawnText& t, bool x_axis, float edge) {
    auto near = [&](float a) { return std::abs(a - edge) <= 1.0F; };
    if (t.item != 0 && (x_axis ? near(t.item_rect.Min.x) || near(t.item_rect.Max.x)
                               : near(t.item_rect.Min.y) || near(t.item_rect.Max.y)))
        return "its control";
    if (t.table_column >= 0 && x_axis) return "its table column";
    if (t.window != nullptr) {
        const ImRect& c = t.window->InnerClipRect;
        if (x_axis ? near(c.Min.x) || near(c.Max.x) : near(c.Min.y) || near(c.Max.y)) return "its window";
    }
    return "its clipping rectangle";
}

}  // namespace

TextLint::TextLint(UiHarness& ui, std::string config, std::filesystem::path shots)
    : ui_(ui), config_(std::move(config)), shots_(std::move(shots)) {}

void TextLint::add(const std::string& where, const std::string& what) {
    const std::string line = config_ + " | " + where + " | " + what;
    if (seen_.insert(line).second) issues.push_back(line);
}

void TextLint::analyse(const std::string& where, const std::vector<DrawnText>& drawn,
                       const app::gui_detail::HelpRecord& help) {
    ++frames;
    issues_before_ = issues.size();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImRect screen(ImVec2(0, 0), display);
    std::map<ImGuiID, const app::ProbeItem*> items;
    std::vector<const app::ProbeItem*> controls;
    for (const app::ProbeItem& it : ui_.items()) {
        items[it.id] = &it;
        if (is_control(it)) controls.push_back(&it);
    }
    std::vector<ImRect> marks;
    std::vector<ImRect> visible(drawn.size());
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        const DrawnText& t = drawn[i];
        ++texts;
        const std::string win(key_of(t.window_name.empty() ? std::string_view("(overlay)") : std::string_view(t.window_name)));
        std::string name = shorten(t.ellipsis ? t.full : t.text);
        if (const auto it = items.find(t.item); t.item != 0 && it != items.end() && !it->second->label.empty())
            name += " (" + std::string(it->second->key()) + ")";
        const std::string at = name + " in " + win;
        const std::size_t before = issues.size();
        ImRect vis = overlap(t.bounds, t.clip);
        if (vis.Min.x > vis.Max.x || vis.Min.y > vis.Max.y) vis = ImRect();  // nothing seen
        visible[i] = vis;
        if (!t.ellipsis || !t.text.empty()) {
            const float right = t.bounds.Max.x - t.clip.Max.x, left = t.clip.Min.x - t.bounds.Min.x;
            const float bottom = t.bounds.Max.y - t.clip.Max.y, top = t.clip.Min.y - t.bounds.Min.y;
            struct Side {
                float over;
                bool x;
                float edge;
                const char* name;
            };
            // The plan views pan and zoom: their labels move out of view
            // with the plan, as a scrolled window's texts do.
            const auto owner = items.find(t.item);
            const bool pans = t.item != 0 && owner != items.end() &&
                              (owner->second->key() == "plan.canvas" || owner->second->key() == "pi.canvas");
            for (const Side& s : {Side{right, true, t.clip.Max.x, "right"}, Side{left, true, t.clip.Min.x, "left"},
                                  Side{bottom, false, t.clip.Max.y, "bottom"}, Side{top, false, t.clip.Min.y, "top"}})
                if (s.over > kTol && !scrolls_there(t.window, s.x, s.edge) && !(pans && std::string(clipper(t, s.x, s.edge)) == "its control"))
                    add(where, at + ": clipped by " + clipper(t, s.x, s.edge) + ", " + px(s.over) + " px at the " + s.name);
            if (vis.GetWidth() > 0.0F) {
                if (vis.Max.x > display.x + kTol || vis.Min.x < -kTol)
                    add(where, at + ": runs past the screen by " +
                                   px(std::max(vis.Max.x - display.x, -vis.Min.x)) + " px");
                if (vis.Max.y > display.y + kTol || vis.Min.y < -kTol)
                    add(where, at + ": runs past the screen by " +
                                   px(std::max(vis.Max.y - display.y, -vis.Min.y)) + " px (vertically)");
            }
        }
        if (t.ellipsis) {
            bool tip = false;
            const std::string full = t.full.substr(0, t.full.find("##"));
            for (const auto& e : help.entries)
                if (e.rect.Min.x - 2 <= t.ellipsis_at.x && t.ellipsis_at.x <= e.rect.Max.x + 2 &&
                    e.rect.Min.y - 2 <= t.ellipsis_at.y && t.ellipsis_at.y <= e.rect.Max.y + 2 &&
                    e.text.find(full) != std::string::npos)
                    tip = true;
            if (!tip) add(where, at + ": shortened with \"...\" (" + shorten(t.text) + " shown) without the whole text in its tooltip");
        }
        // The label of a control stays inside it (a text field's value and
        // its label beside the field excepted).
        if (const auto it = items.find(t.item); t.item != 0 && it != items.end() && is_control(*it->second) &&
                                                (it->second->status & ImGuiItemStatusFlags_Inputable) == 0 &&
                                                t.item_rect.Contains(ImVec2(t.bounds.Min.x + 0.5F, t.bounds.Min.y + 0.5F)) &&
                                                t.bounds.Max.x > t.item_rect.Max.x + kTol)
            add(where, at + ": runs " + px(t.bounds.Max.x - t.item_rect.Max.x) + " px past its control");
        // Over a control it is not the text of.
        if (vis.GetWidth() > 0.0F)
            for (const app::ProbeItem* c : controls) {
                if (c->window != t.window_name || c->id == t.item) continue;
                // (A selectable row reaches half the item spacing into its
                // neighbours, as the UI lint allows for controls.)
                const ImVec2 half(ImGui::GetStyle().ItemSpacing.x * 0.5F, ImGui::GetStyle().ItemSpacing.y * 0.5F);
                const ImRect cr(c->x0 + half.x, c->y0 + half.y, c->x1 - half.x, c->y1 - half.y);
                if (cr.Contains(vis.GetCenter())) continue;
                const ImRect o = overlap(vis, cr);
                if (o.GetWidth() > 1.5F && o.GetHeight() > 1.5F)
                    add(where, at + ": overlaps the control " + std::string(c->key()) + " by " + px(o.GetWidth()) + " x " +
                                   px(o.GetHeight()) + " px");
            }
        if (issues.size() > before) marks.push_back(t.bounds);
    }
    // Texts over each other (in one window's draw list).
    for (std::size_t i = 0; i < drawn.size(); ++i)
        for (std::size_t j = i + 1; j < drawn.size(); ++j) {
            const DrawnText& a = drawn[i];
            const DrawnText& b = drawn[j];
            if (a.list != b.list || visible[i].GetWidth() <= 0.0F || visible[j].GetWidth() <= 0.0F) continue;
            // The plan's labels move with the plan and may pass under its
            // fixed overlays (legend, scale bar), as on any map.
            const auto on_plan = [&](const DrawnText& t) {
                const auto it = items.find(t.item);
                return t.item != 0 && it != items.end() && it->second->key() == "plan.canvas";
            };
            if (on_plan(a) && on_plan(b)) continue;
            // A text drawn twice a pixel apart is its shadow.
            if (a.text == b.text && std::abs(a.bounds.Min.x - b.bounds.Min.x) <= 2.5F &&
                std::abs(a.bounds.Min.y - b.bounds.Min.y) <= 2.5F)
                continue;
            const ImRect o = overlap(visible[i], visible[j]);
            if (o.GetWidth() > kTol && o.GetHeight() > kTol) {
                const std::string win(key_of(a.window_name.empty() ? std::string_view("(overlay)") : std::string_view(a.window_name)));
                add(where, shorten(a.text) + " and " + shorten(b.text) + " in " + win + " overlap by " + px(o.GetWidth()) +
                               " x " + px(o.GetHeight()) + " px");
                marks.push_back(a.bounds);
                marks.push_back(b.bounds);
            }
        }
    if (!marks.empty()) shot(where, marks);
}

void TextLint::tooltips(const std::string& where, const app::gui_detail::HelpRecord& help) {
    const ImVec2 keep = ImGui::GetIO().MousePos;
    for (std::size_t i = 0; i < help.entries.size(); ++i) {
        const auto& e = help.entries[i];
        if (e.text.empty()) continue;
        // Only items a user can point at: inside their window's visible
        // part and on the screen (not scrolled out of view).
        const ImGuiWindow* w = ImGui::FindWindowByID(e.window);
        const ImVec2 c = e.rect.GetCenter();
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        if (w == nullptr || !w->InnerClipRect.Contains(c) || c.x < 0 || c.y < 0 || c.x > d.x || c.y > d.y) continue;
        if (!tips_done_.insert(e.text).second) continue;
        app::gui_detail::HelpRecord rec;
        rec.show = static_cast<int>(i);
        ui_.mouse_move(e.rect.GetCenter().x, e.rect.GetCenter().y);
        app::gui_detail::help_record() = &rec;
        bool ok = ui_.frame();  // the tooltip window takes its size
        // An item under another window (e.g. scrolled under the status bar)
        // cannot be pointed at there.
        const ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
        if (hovered != nullptr && hovered->RootWindow != w->RootWindow) {
            app::gui_detail::help_record() = nullptr;
            continue;
        }
        rec.entries.clear();
        text_probe_start();
        ok = ui_.frame() && ok;
        std::vector<DrawnText> drawn = text_probe_stop();
        app::gui_detail::help_record() = nullptr;
        if (!ok) {
            add(where, "the frame failed: " + ui_.last_error());
            continue;
        }
        // Only the tooltip's texts (the page was checked without it).
        std::vector<DrawnText> tip;
        for (DrawnText& t : drawn)
            if (t.window != nullptr && (t.window->Flags & ImGuiWindowFlags_Tooltip) != 0) tip.push_back(std::move(t));
        if (tip.empty()) continue;  // the item is not where it was (a list that moved)
        ++tooltips_drawn;
        analyse(where + " tooltip " + shorten(e.text), tip, rec);
    }
    ui_.mouse_move(keep.x, keep.y);
}

void TextLint::shot(const std::string& where, const std::vector<ImRect>& marks) {
    if (shots_taken_ >= 20) return;
    std::string stem = config_ + "_" + where;
    for (char& c : stem)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    if (stem.size() > 120) stem.resize(120);
    const std::filesystem::path f = shots_ / (stem + "_" + std::to_string(shots_taken_++) + ".png");
    std::filesystem::create_directories(shots_);
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    if (write_frame_png(f, static_cast<int>(d.x), static_cast<int>(d.y), marks))
        issues.push_back(config_ + " | " + where + " | screenshot: " + f.generic_string());
}

bool write_frame_png(const std::filesystem::path& file, int width, int height, const std::vector<ImRect>& marks) {
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd == nullptr || width <= 0 || height <= 0) return false;
    plan::Raster img(width, height, 0);
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    std::map<ImTextureID, const ImTextureData*> textures;
    if (dd->Textures != nullptr)
        for (const ImTextureData* t : *dd->Textures) textures[t->TexID] = t;
    auto blend = [&](int x, int y, float r, float g, float b, float a) {
        std::uint8_t* p = img.pixel(x, y);
        p[0] = static_cast<std::uint8_t>(std::lround(r * 255 * a + p[0] * (1 - a)));
        p[1] = static_cast<std::uint8_t>(std::lround(g * 255 * a + p[1] * (1 - a)));
        p[2] = static_cast<std::uint8_t>(std::lround(b * 255 * a + p[2] * (1 - a)));
    };
    for (const ImDrawList* list : dd->CmdLists) {
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr) continue;
            const auto it = textures.find(cmd.GetTexID());
            const ImTextureData* tex = it == textures.end() ? nullptr : it->second;
            const int cx0 = std::max(0, static_cast<int>(cmd.ClipRect.x)), cy0 = std::max(0, static_cast<int>(cmd.ClipRect.y));
            const int cx1 = std::min(width, static_cast<int>(std::ceil(cmd.ClipRect.z)));
            const int cy1 = std::min(height, static_cast<int>(std::ceil(cmd.ClipRect.w)));
            for (unsigned e = 0; e + 2 < cmd.ElemCount; e += 3) {
                const ImDrawVert* v[3];
                for (int k = 0; k < 3; ++k) v[k] = &list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + e + k]];
                const float area = (v[1]->pos.x - v[0]->pos.x) * (v[2]->pos.y - v[0]->pos.y) -
                                   (v[2]->pos.x - v[0]->pos.x) * (v[1]->pos.y - v[0]->pos.y);
                if (std::abs(area) < 1e-6F) continue;
                const int x0 = std::max(cx0, static_cast<int>(std::floor(std::min({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}))));
                const int x1 = std::min(cx1, static_cast<int>(std::ceil(std::max({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}))));
                const int y0 = std::max(cy0, static_cast<int>(std::floor(std::min({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}))));
                const int y1 = std::min(cy1, static_cast<int>(std::ceil(std::max({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}))));
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        const float px_ = static_cast<float>(x) + 0.5F, py = static_cast<float>(y) + 0.5F;
                        float w[3];
                        for (int k = 0; k < 3; ++k) {
                            const ImDrawVert* a = v[(k + 1) % 3];
                            const ImDrawVert* b = v[(k + 2) % 3];
                            w[k] = ((b->pos.x - a->pos.x) * (py - a->pos.y) - (b->pos.y - a->pos.y) * (px_ - a->pos.x)) / area;
                        }
                        if (w[0] < 0 || w[1] < 0 || w[2] < 0) continue;
                        float c[4] = {0, 0, 0, 0}, u = 0, vv = 0;
                        for (int k = 0; k < 3; ++k) {
                            const ImU32 col = v[k]->col;
                            for (int ch = 0; ch < 4; ++ch) c[ch] += w[k] * static_cast<float>((col >> (8 * ch)) & 0xFF) / 255.0F;
                            u += w[k] * v[k]->uv.x;
                            vv += w[k] * v[k]->uv.y;
                        }
                        float ta = 1.0F, tr = 1.0F, tg = 1.0F, tb = 1.0F;
                        if (tex != nullptr && tex->Pixels != nullptr) {
                            const int tx = std::clamp(static_cast<int>(u * static_cast<float>(tex->Width)), 0, tex->Width - 1);
                            const int ty = std::clamp(static_cast<int>(vv * static_cast<float>(tex->Height)), 0, tex->Height - 1);
                            const unsigned char* p = tex->Pixels + (static_cast<std::size_t>(ty) * tex->Width + tx) * tex->BytesPerPixel;
                            if (tex->BytesPerPixel == 1) ta = p[0] / 255.0F;
                            else tr = p[0] / 255.0F, tg = p[1] / 255.0F, tb = p[2] / 255.0F, ta = p[3] / 255.0F;
                        } else if (tex == nullptr) {
                            tr = tg = tb = 0.5F;  // a plan or logo: grey
                        }
                        blend(x, y, c[0] * tr, c[1] * tg, c[2] * tb, c[3] * ta);
                    }
            }
        }
    }
    for (const ImRect& m : marks) {
        const int x0 = std::clamp(static_cast<int>(m.Min.x) - 2, 0, width - 1), x1 = std::clamp(static_cast<int>(m.Max.x) + 2, 0, width - 1);
        const int y0 = std::clamp(static_cast<int>(m.Min.y) - 2, 0, height - 1), y1 = std::clamp(static_cast<int>(m.Max.y) + 2, 0, height - 1);
        for (int x = x0; x <= x1; ++x)
            for (int y : {y0, y1}) blend(x, y, 1, 0, 0, 1);
        for (int y = y0; y <= y1; ++y)
            for (int x : {x0, x1}) blend(x, y, 1, 0, 0, 1);
    }
    const plan::fixtures::Bytes png = plan::fixtures::png(img, 4);
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

}  // namespace yantra::test
