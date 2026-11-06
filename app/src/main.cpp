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

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "gui.h"
#include "yantra/app/app_icon.h"
#include "yantra/app/app_state.h"
#include "yantra/app/cli.h"
#include "yantra/app/data_paths.h"
#include "yantra/app/settings.h"
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
    const bool screenshot = cli.screenshot.has_value();

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
    paths.data_folders = app::data_folders(inputs);
    paths.user_data = app::user_data_folder(inputs);
    if (fs::path(inputs.home).is_absolute()) paths.home = inputs.home;

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
    app::Gui::apply_style();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 140");

    int rc = 0;
    {
        app::AppState state(settings, paths);
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
            if (!r.ok) std::fprintf(stderr, "yantra: --dialog %s: %s\n", cli.dialog.c_str(), r.message.c_str());
        }
        app::Gui gui(state);

        std::string title;
        // A screenshot is taken after a few frames, so that windows and
        // popups have settled (no fade-in left).
        const int capture_frame = 5;
        for (int frame = 1;; ++frame) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window) != 0 || state.quit_requested()) break;
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            // A screenshot advances a fixed time per frame, so popups have
            // faded in by the capture frame whatever the machine's speed.
            if (screenshot) ImGui::GetIO().DeltaTime = 1.0F / 15.0F;
            ImGui::NewFrame();
            gui.draw();
            ImGui::Render();
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(window, &fw, &fh);
            glViewport(0, 0, fw, fh);
            glClearColor(0.08F, 0.08F, 0.09F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (screenshot && frame == capture_frame) {
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
            if (screenshot && frame >= capture_frame) break;
            if (cli.frames > 0 && frame >= cli.frames) break;
        }
        if (!screenshot) {
            std::string error;
            if (!state.save_settings(error)) std::fprintf(stderr, "yantra: %s\n", error.c_str());
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return rc;
}
