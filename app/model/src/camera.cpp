// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the photo source and the camera capture (camera.h): the fake
// camera, the pixel conversions, photos from files and frames, and the
// preview worker. The system backends are camera_v4l2.cpp (Linux) and
// camera_mf.cpp (Windows).

#include "yantra/app/camera.h"

#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "yantra/core/about/photo.h"
#include "yantra/core/export/raster.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

std::uint8_t clamp8(int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); }

class FakeStream final : public CameraStream {
public:
    bool next(CameraImage& out, std::string&) override {
        // About 15 frames a second, as a slow webcam.
        std::this_thread::sleep_for(std::chrono::milliseconds(66));
        out = fake_camera_image(640, 480, frame_++);
        return true;
    }
    std::string format() const override { return "test image 640x480"; }

private:
    std::uint64_t frame_ = 0;
};

class FakeBackend final : public CameraBackend {
public:
    std::string name() const override { return "fake"; }
    std::vector<CameraInfo> cameras() override { return {CameraInfo{"fake", "Fake camera (test image)"}}; }
    std::unique_ptr<CameraStream> open(const std::string& id, std::string& error) override {
        if (id != "fake") {
            error = "no such camera: " + id;
            return nullptr;
        }
        return std::make_unique<FakeStream>();
    }
};

class NoBackend final : public CameraBackend {
public:
    std::string name() const override { return "none"; }
    std::vector<CameraInfo> cameras() override { return {}; }
    std::unique_ptr<CameraStream> open(const std::string&, std::string& error) override {
        error = "no camera";
        return nullptr;
    }
};

}  // namespace

std::shared_ptr<CameraBackend> make_fake_camera_backend() { return std::make_shared<FakeBackend>(); }
std::shared_ptr<CameraBackend> make_no_camera_backend() { return std::make_shared<NoBackend>(); }

#if !defined(__linux__) && !defined(_WIN32)
std::shared_ptr<CameraBackend> make_system_camera_backend() { return make_no_camera_backend(); }
#endif

std::shared_ptr<CameraBackend> make_camera_backend() {
    const char* fake = std::getenv("YANTRA_FAKE_CAMERA");
    if (fake == nullptr || *fake == '\0') return make_system_camera_backend();
    if (std::string(fake) == "none") return make_no_camera_backend();
    return make_fake_camera_backend();
}

void yuyv_to_rgba(const std::uint8_t* src, int width, int height, std::size_t stride, std::vector<std::uint8_t>& rgba) {
    rgba.assign(static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)) * 4u, 255);
    if (width <= 0 || height <= 0 || src == nullptr) return;
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = src + static_cast<std::size_t>(y) * stride;
        std::uint8_t* out = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        for (int x = 0; x < width; ++x) {
            const std::size_t pair = static_cast<std::size_t>(x / 2) * 4u;
            const int luma = row[pair + ((x % 2) != 0 ? 2u : 0u)];
            const int u = row[pair + 1] - 128;
            const int v = row[pair + 3] - 128;
            // BT.601, limited range (16..235), in fixed point (x 256).
            const int c = 298 * (luma - 16);
            out[4 * x + 0] = clamp8((c + 409 * v + 128) >> 8);
            out[4 * x + 1] = clamp8((c - 100 * u - 208 * v + 128) >> 8);
            out[4 * x + 2] = clamp8((c + 516 * u + 128) >> 8);
            out[4 * x + 3] = 255;
        }
    }
}

bool decode_mjpeg(std::span<const std::uint8_t> jpeg, CameraImage& out) {
    if (jpeg.empty() || jpeg.size() > static_cast<std::size_t>(INT_MAX)) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &w, &h, &comp, 4);
    if (px == nullptr) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    stbi_image_free(px);
    return true;
}

CameraImage fake_camera_image(int width, int height, std::uint64_t frame) {
    CameraImage img;
    img.width = std::max(16, width);
    img.height = std::max(16, height);
    img.rgba.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4u, 255);
    static const std::uint8_t bars[8][3] = {{235, 235, 235}, {235, 235, 16}, {16, 235, 235}, {16, 235, 16},
                                            {235, 16, 235},  {235, 16, 16},  {16, 16, 235},  {16, 16, 16}};
    const int bar_h = img.height * 2 / 3;
    const int box = std::max(8, img.height / 8);
    const int travel = std::max(1, img.width - box);
    const int bx = static_cast<int>(frame * 8 % static_cast<std::uint64_t>(travel));
    const int by = bar_h + (img.height - bar_h - box) / 2;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            std::uint8_t* p = img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                                                 static_cast<std::size_t>(x)) * 4u;
            if (y < bar_h) {
                const std::uint8_t* c = bars[std::min(7, x * 8 / img.width)];
                p[0] = c[0];
                p[1] = c[1];
                p[2] = c[2];
            } else {
                const auto g = static_cast<std::uint8_t>(x * 255 / std::max(1, img.width - 1));
                p[0] = p[1] = p[2] = g;
            }
            if (x >= bx && x < bx + box && y >= by && y < by + box) {
                p[0] = 255;
                p[1] = 140;
                p[2] = 0;
            }
        }
    return img;
}

std::optional<Photo> PhotoSource::from_file(const fs::path& file, std::string& error) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) {
        error = "cannot read " + file.filename().string();
        return std::nullopt;
    }
    if (size > project::kMaxPhotoBytes) {
        error = file.filename().string() + " is larger than 32 MiB";
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    Photo p;
    p.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) {
        error = "cannot read " + file.filename().string();
        return std::nullopt;
    }
    if (!about::is_photo(p.bytes)) {
        error = file.filename().string() + " is not an image Yantra reads (JPEG, PNG, BMP, GIF, TGA, PSD, HDR, PNM)";
        return std::nullopt;
    }
    p.name = file.filename().string();
    p.source = "file";
    return p;
}

Photo PhotoSource::from_camera(const CameraImage& frame) {
    exports::Canvas c(frame.width, frame.height);
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            const std::uint8_t* s = frame.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                                                         static_cast<std::size_t>(x)) * 4u;
            c.set(x, y, exports::Rgba8{s[0], s[1], s[2], 255});
        }
    Photo p;
    p.bytes = exports::encode_jpg(c, 90);
    p.name = "camera.jpg";
    p.source = "camera";
    return p;
}

// ---- CameraCapture ----------------------------------------------------------

CameraCapture::CameraCapture(std::shared_ptr<CameraBackend> backend)
    : backend_(backend ? std::move(backend) : make_no_camera_backend()) {}

CameraCapture::~CameraCapture() { stop(); }

std::vector<CameraInfo> CameraCapture::cameras() { return backend_->cameras(); }

void CameraCapture::start(const std::string& id) {
    stop();
    {
        const std::lock_guard l(mutex_);
        error_.clear();
        format_.clear();
        latest_ = {};
        frames_ = 0;
    }
    stop_ = false;
    state_ = static_cast<int>(State::Starting);
    worker_ = std::thread([this, id] { run(id); });
}

void CameraCapture::stop() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    if (state() != State::Failed) state_ = static_cast<int>(State::Idle);
}

std::string CameraCapture::error() const {
    const std::lock_guard l(mutex_);
    return error_;
}

std::string CameraCapture::format() const {
    const std::lock_guard l(mutex_);
    return format_;
}

std::uint64_t CameraCapture::frame(CameraImage* out) const {
    const std::lock_guard l(mutex_);
    if (out != nullptr && frames_ > 0) *out = latest_;
    return frames_;
}

std::optional<Photo> CameraCapture::take() const {
    CameraImage img;
    if (frame(&img) == 0 || img.empty()) return std::nullopt;
    return PhotoSource::from_camera(img);
}

void CameraCapture::run(std::string id) {
    std::string error;
    std::unique_ptr<CameraStream> s = backend_->open(id, error);
    if (!s) {
        const std::lock_guard l(mutex_);
        error_ = error.empty() ? "the camera cannot be opened" : error;
        state_ = static_cast<int>(State::Failed);
        return;
    }
    {
        const std::lock_guard l(mutex_);
        format_ = s->format();
    }
    int failures = 0;
    while (!stop_) {
        CameraImage img;
        if (!s->next(img, error) || img.empty()) {
            // A few lost frames are normal (a camera starting up); then give up.
            if (++failures < 5) continue;
            const std::lock_guard l(mutex_);
            error_ = error.empty() ? "the camera sends no pictures" : error;
            state_ = static_cast<int>(State::Failed);
            return;
        }
        failures = 0;
        const std::lock_guard l(mutex_);
        latest_ = std::move(img);
        ++frames_;
        state_ = static_cast<int>(State::Running);
    }
}

}  // namespace yantra::app
