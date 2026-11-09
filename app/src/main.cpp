// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Yantra desktop app (OA-17, UI-13): GLFW window with an OpenGL 3 context,
// Dear ImGui, the app state model (app/model) and its ImGui layer
// (app/gui). Window system choice per NFR-10; window icon from the Yantra
// logo (BRD-06); --screenshot renders offscreen and saves a PNG (SCR-01,
// TST-04); --frames N exits after N frames (launch tests).
// Copied from yantra-v2 68ea20a: app/src/main.cpp, adapted (data folders,
// window icon, no plugins yet).
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stb_image_write.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "curl_downloader.h"
#include "curl_sender.h"
#include "gui.h"
#include "screenshot.h"
#include "ui_probe.h"
#include "miniaudio_io.h"
#include "yantra/app/app_icon.h"
#include "yantra/app/app_state.h"
#include "yantra/app/cli.h"
#include "yantra/app/data_paths.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/plugin/folders.h"
#include "yantra/app/settings.h"
#include "yantra/app/settings_reset.h"
#include "yantra/app/window_system.h"
#include "yantra/core/build_info.h"

namespace fs = std::filesystem;
namespace app = yantra::app;

namespace {

void glfw_error(int code, const char* text) { std::fprintf(stderr, "yantra: GLFW error %d: %s\n", code, text); }

int glfw_platform(app::WindowSystem s) {
    switch (s) {
    case app::WindowSystem::X11: return GLFW_PLATFORM_X11;
    case app::WindowSystem::Wayland: return GLFW_PLATFORM_WAYLAND;
    case app::WindowSystem::Auto: break;
    }
    return GLFW_ANY_PLATFORM;
}

const char* platform_name(int p) {
    switch (p) {
    case GLFW_PLATFORM_X11: return "x11";
    case GLFW_PLATFORM_WAYLAND: return "wayland";
    case GLFW_PLATFORM_NULL: return "null";
    default: return "other";
    }
}

/// Reads the back buffer of the current frame and writes it as PNG.
bool save_png(const fs::path& file, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;  // opaque
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(file.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

/// BRD-06: the Yantra logo as the window icon. Wayland has no window
/// icons (the desktop file's icon is used there), so it is set on X11
/// only.
void set_window_icon(GLFWwindow* window, const std::vector<fs::path>& data_folders) {
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        std::fprintf(stderr, "yantra: window icon from the desktop file (Wayland)\n");
        return;
    }
    std::string error;
    std::vector<yantra::plan::Raster> icons = app::load_app_icons(data_folders, error);
    if (!error.empty()) std::fprintf(stderr, "yantra: window icon: %s\n", error.c_str());
    if (icons.empty()) return;
    std::vector<GLFWimage> images;
    for (yantra::plan::Raster& r : icons) images.push_back(GLFWimage{r.width, r.height, r.rgba.data()});
    glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());
    std::fprintf(stderr, "yantra: window icon set (%zu sizes, %dx%d to %dx%d)\n", images.size(), images.front().width,
                 images.front().height, images.back().width, images.back().height);
}

}  // namespace

int main(int argc, char** argv) {
    const auto& info = yantra::core::build_info();
    const app::ParseResult parsed = app::parse_command_line(std::vector<std::string>(argv + 1, argv + argc));
    if (!parsed.command_line) {
        std::fprintf(stderr, "yantra: %s\n", parsed.error.c_str());
        return 2;
    }
    const app::CommandLine& cli = *parsed.command_line;
    if (cli.show_version) {
        std::printf("yantra %.*s\n", static_cast<int>(info.version_string.size()), info.version_string.data());
        return 0;
    }
    if (cli.show_help) {
        std::fputs(app::usage_text().c_str(), stdout);
        return 0;
    }
    if (cli.list_shots) {
        // One scene per line: name, flags (demo, touch, radio=..., input), what.
        for (const app::ShotScene& s : app::shot_scenes()) {
            std::string flags = s.demo ? "demo" : "-";
            if (s.touch) flags += ",touch";
            if (!s.radio.empty()) flags += ",radio=" + s.radio;
            if (s.needs_input) flags += ",input";
            if (!s.plugin.empty()) flags += ",plugin=" + s.plugin;
            std::printf("%s\t%s\t%s\n", s.name.c_str(), flags.c_str(), s.what.c_str());
        }
        return 0;
    }
    const bool screenshot = cli.screenshot.has_value();
    const app::ShotScene* shot = nullptr;
    if (!cli.shot.empty()) {
        shot = app::find_shot_scene(cli.shot);
        if (shot == nullptr) {
            std::string list;
            for (const app::ShotScene& s : app::shot_scenes()) list += " " + s.name;
            std::fprintf(stderr, "yantra: --shot %s: no such scene; scenes:%s\n", cli.shot.c_str(), list.c_str());
            return 2;
        }
        if (shot->needs_input && !cli.input) {
            std::fprintf(stderr, "yantra: --shot %s needs --input FILE\n", cli.shot.c_str());
            return 2;
        }
    }

    // Settings: a screenshot run uses the defaults and writes nothing, so
    // its picture does not depend on the user's recent files.
    const app::DataPathInputs inputs = app::data_path_inputs_from_environment();
    app::AppPaths paths;
    app::AppSettings settings;
    const fs::path config = app::user_config_folder(inputs);
    if (!screenshot && !config.empty()) {
        paths.settings_file = config / "app.json";
        std::string error;
        settings = app::load_settings(paths.settings_file, error);
        if (!error.empty()) std::fprintf(stderr, "yantra: %s: %s\n", paths.settings_file.c_str(), error.c_str());
    }
    // SCR-01: the run's look from the command line (never saved).
    if (cli.theme) settings.theme = *cli.theme;
    if (cli.language) settings.language = app::language_key(*cli.language);
    if (cli.touch) settings.touch = true;
    if (cli.scale) settings.ui_scale = *cli.scale;
    if (shot != nullptr) settings = app::shot_settings(*shot, settings);
    paths.data_folders = app::data_folders(inputs);
    paths.user_data = app::user_data_folder(inputs);
    if (fs::path(inputs.home).is_absolute()) paths.home = inputs.home;
    {
        // The walk-test view's radios (PLG-09 folder rule) and the user's
        // calibration table (RF-02).
        const yantra::plugin::PluginFolders pf = yantra::plugin::plugin_folders(yantra::plugin::folder_inputs_from_environment());
        paths.radios.plugin_folders = pf.plugins;
        paths.radios.radio_folders = pf.radios;
        paths.radios.calibration_table = yantra::calibration::default_table_path();
        // RAD-47 / NFR-05: the radios' stored settings next to the app settings
        // (a screenshot run keeps them in memory).
        if (!screenshot && !config.empty()) paths.radios.plugin_state = config / "plugin_state.json";
        // REC-09: the ring buffer's temporary files in the user's cache folder;
        // DEMO-12: the tour's MAK sheets there too (YANTRA_MAK_CACHE overrides).
        if (!screenshot) {
            const char* cache = std::getenv("XDG_CACHE_HOME");
            fs::path user_cache;
            if (cache != nullptr && fs::path(cache).is_absolute()) user_cache = fs::path(cache) / "yantra";
            else if (!paths.home.empty()) user_cache = paths.home / ".cache" / "yantra";
            if (!user_cache.empty()) {
                paths.radios.ring_folder = user_cache / "ring";
                paths.sheet_cache = user_cache / "mak";
            }
            const char* mak = std::getenv("YANTRA_MAK_CACHE");
            if (mak != nullptr && fs::path(mak).is_absolute()) paths.sheet_cache = mak;
        }
    }

    // UI-16: --reset-settings does what Help > Reset all settings does,
    // without the UI, and exits.
    if (cli.reset_settings) {
        if (config.empty()) {
            std::fprintf(stderr, "yantra: no settings folder (set HOME or XDG_CONFIG_HOME)\n");
            return 1;
        }
        const app::ResetOutcome o = app::reset_settings_files(
            app::SettingsFiles{config / "app.json", config / "plugin_state.json", paths.radios.calibration_table},
            cli.reset_calibration, std::time(nullptr));
        std::printf("yantra: %s\n", o.message.c_str());
        return o.ok ? 0 : 1;
    }

    // Window system (NFR-10).
    const app::WindowSystemChoice ws =
        app::choose_window_system(app::window_system_inputs(cli.window_system, settings.window_system));
    if (!ws.warning.empty()) std::fprintf(stderr, "yantra: %s\n", ws.warning.c_str());
    glfwSetErrorCallback(glfw_error);
    int platform = glfw_platform(ws.system);
    if (platform != GLFW_ANY_PLATFORM && glfwPlatformSupported(platform) == GLFW_FALSE) {
        std::fprintf(stderr, "yantra: this GLFW has no %s support; letting GLFW choose\n", app::to_string(ws.system));
        platform = GLFW_ANY_PLATFORM;
    }
    glfwInitHint(GLFW_PLATFORM, platform);
    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "yantra: cannot start the window system (%s); try --window-system x11 or wayland\n",
                     ws.reason.c_str());
        return 1;
    }
    std::fprintf(stderr, "yantra: window system %s (%s)\n", platform_name(glfwGetPlatform()), ws.reason.c_str());

    // OpenGL 3.1 is enough (TST-04: Mesa software rendering).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_VISIBLE, screenshot ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, screenshot ? GLFW_FALSE : GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(cli.width, cli.height, "Yantra", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "yantra: cannot open a window with OpenGL 3.1\n");
        glfwTerminate();
        return 1;
    }
    set_window_icon(window, paths.data_folders);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(screenshot || cli.frames > 0 ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // fixed layout for now; window layouts come with the docking windows
    app::Gui::apply_style(app::Gui::style_of(settings));
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 140");

    int rc = 0;
    {
        app::AppState state(settings, paths);
        // REC-06: voice notes through miniaudio (none for a screenshot).
        if (!screenshot) state.set_audio(app::make_miniaudio_io());
        // DEMO-12: the one-time sheet download of the tour (none in a screenshot).
        if (!screenshot) state.set_downloader(app::make_curl_downloader());
        if (!screenshot) state.set_sender(app::make_curl_sender());  // EXP-07
        // DEMO-01: demo mode opens the demo project; the simulator is the
        // default radio there (LiveSurvey).
        if (cli.demo) {
            const app::Result r = state.open_demo();
            if (!r.ok) std::fprintf(stderr, "yantra: --demo: %s\n", r.message.c_str());
        }
        if (cli.tour) {
            const app::Result r = state.trigger(app::Action::Tour);
            if (!r.ok) std::fprintf(stderr, "yantra: --tour: %s\n", r.message.c_str());
        }
        if (cli.project) {
            const app::Result r = state.open_project(*cli.project);
            if (!r.ok) std::fprintf(stderr, "yantra: %s\n", r.message.c_str());
        }
        if (!cli.dialog.empty()) {
            app::Result r;
            if (cli.dialog == "new-project") r = state.trigger(app::Action::NewProject);
            if (cli.dialog == "open-project") r = state.trigger(app::Action::OpenProject);
            if (cli.dialog == "save-as") r = state.trigger(app::Action::SaveAs);
            if (cli.dialog == "add-floor") r = state.trigger(app::Action::AddFloor);
            if (cli.dialog == "about") r = state.trigger(app::Action::About);
            if (cli.dialog == "settings") r = state.trigger(app::Action::Settings);
            if (cli.dialog == "reset-settings") r = state.trigger(app::Action::ResetSettings);
            if (!r.ok) std::fprintf(stderr, "yantra: --dialog %s: %s\n", cli.dialog.c_str(), r.message.c_str());
        }
        app::Gui gui(state);
        // The item probe: the tour points at the controls it is about
        // (their rectangles of the last frame).
        app::UiProbe probe(ImGui::GetCurrentContext());
        // A screenshot shows the plan with its tiles, not while they render.
        gui.set_wait_for_tiles(screenshot);
        // SCR-01..03: a scene of the screenshot set, played before the capture.
        std::unique_ptr<app::ShotRunner> runner;
        if (shot != nullptr) {
            app::SystemStatus fixed;
            fixed.free_bytes = 120ULL * 1000ULL * 1000ULL * 1000ULL;
            fixed.on_mains = true;
            state.fix_system_status(fixed);
            runner = std::make_unique<app::ShotRunner>(*shot, state, cli.input.value_or(fs::path{}));
        }

        std::string title;
        // A screenshot is taken after a few frames, so that windows and
        // popups have settled (no fade-in left).
        const int capture_frame = 5;
        for (int frame = 1;; ++frame) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window) != 0) {
                // REC-11: no accidental exit while recording.
                if (state.request_exit().ok) break;
                glfwSetWindowShouldClose(window, GLFW_FALSE);
            }
            if (state.quit_requested()) break;
            if (runner) {
                runner->before_frame(probe.items());
                if (runner->failed()) {
                    std::fprintf(stderr, "yantra: --shot %s: %s\n", shot->name.c_str(), runner->error().c_str());
                    rc = 1;
                    break;
                }
                // Waiting on a radio or a worker: no need to spin.
                if (runner->waiting()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            // A screenshot advances a fixed time per frame, so popups have
            // faded in by the capture frame whatever the machine's speed.
            if (screenshot) ImGui::GetIO().DeltaTime = 1.0F / 15.0F;
            probe.begin_frame();
            ImGui::NewFrame();
            gui.draw();
            ImGui::Render();
            probe.end_frame();
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(window, &fw, &fh);
            glViewport(0, 0, fw, fh);
            glClearColor(0.08F, 0.08F, 0.09F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            const bool capture = runner ? runner->ready() : frame == capture_frame;
            if (screenshot && capture) {
                if (!save_png(*cli.screenshot, fw, fh)) {
                    std::fprintf(stderr, "yantra: cannot write %s\n", cli.screenshot->c_str());
                    rc = 1;
                } else {
                    std::printf("yantra: screenshot %s (%dx%d)\n", cli.screenshot->c_str(), fw, fh);
                }
            }
            glfwSwapBuffers(window);
            if (frame == 1) {
                const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
                const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
                std::printf("yantra: first frame (%s, OpenGL %s, %s)\n", platform_name(glfwGetPlatform()),
                            version != nullptr ? version : "?", renderer != nullptr ? renderer : "?");
                std::fflush(stdout);
            }
            if (const std::string t = state.window_title(); t != title) {
                title = t;
                glfwSetWindowTitle(window, title.c_str());
            }
            if (screenshot && capture) break;
            if (cli.frames > 0 && frame >= cli.frames) break;
        }
        if (!screenshot) {
            std::string error;
            if (!state.save_settings(error)) std::fprintf(stderr, "yantra: %s\n", error.c_str());
        }
        // The renderer destroys every texture, the plan's too, before the
        // Gui frees them (app/gui/user_texture.h).
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return rc;
}
