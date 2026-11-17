// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47, spec 16.2: Linux cameras through V4L2, Yantra's own code (no
// camera library). The capture devices are the /dev/video* nodes whose
// device capabilities include video capture and streaming; a camera is
// opened with MJPEG (preferred: less USB bandwidth at high resolutions,
// decoded with stb_image) or YUYV (converted by yuyv_to_rgba) at up to
// 1280x720, with four memory-mapped buffers.

#include "yantra/app/camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

int xioctl(int fd, unsigned long request, void* arg) {
    int r = 0;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

std::string os_error(const std::string& what) { return what + ": " + std::strerror(errno); }

bool can_capture(int fd, std::string* name) {
    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) return false;
    const std::uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? cap.device_caps : cap.capabilities;
    if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0 || (caps & V4L2_CAP_STREAMING) == 0) return false;
    if (name != nullptr) *name = reinterpret_cast<const char*>(cap.card);
    return true;
}

class V4l2Stream final : public CameraStream {
public:
    ~V4l2Stream() override {
        if (fd_ >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            (void)xioctl(fd_, VIDIOC_STREAMOFF, &type);
        }
        for (const Buffer& b : buffers_)
            if (b.start != MAP_FAILED) munmap(b.start, b.length);
        if (fd_ >= 0) close(fd_);
    }

    bool open_device(const std::string& path, std::string& error) {
        fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            error = os_error("cannot open " + path);
            return false;
        }
        if (!can_capture(fd_, nullptr)) {
            error = path + " is not a camera";
            return false;
        }
        // MJPEG if the camera offers it, else YUYV.
        bool mjpeg = false, yuyv = false;
        v4l2_fmtdesc desc{};
        desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        for (desc.index = 0; xioctl(fd_, VIDIOC_ENUM_FMT, &desc) == 0; ++desc.index) {
            mjpeg = mjpeg || desc.pixelformat == V4L2_PIX_FMT_MJPEG || desc.pixelformat == V4L2_PIX_FMT_JPEG;
            yuyv = yuyv || desc.pixelformat == V4L2_PIX_FMT_YUYV;
        }
        if (!mjpeg && !yuyv) {
            error = path + " offers neither MJPEG nor YUYV pictures";
            return false;
        }
        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 1280;
        fmt.fmt.pix.height = 720;
        fmt.fmt.pix.pixelformat = mjpeg ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (xioctl(fd_, VIDIOC_S_FMT, &fmt) == -1) {
            error = os_error("cannot set the picture format of " + path);
            return false;
        }
        pixelformat_ = fmt.fmt.pix.pixelformat;
        width_ = static_cast<int>(fmt.fmt.pix.width);
        height_ = static_cast<int>(fmt.fmt.pix.height);
        stride_ = std::max<std::size_t>(fmt.fmt.pix.bytesperline, static_cast<std::size_t>(width_) * 2u);
        if (pixelformat_ != V4L2_PIX_FMT_MJPEG && pixelformat_ != V4L2_PIX_FMT_JPEG && pixelformat_ != V4L2_PIX_FMT_YUYV) {
            error = path + ": the driver chose another picture format";
            return false;
        }
        v4l2_requestbuffers req{};
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_REQBUFS, &req) == -1 || req.count < 2) {
            error = os_error("cannot get capture buffers of " + path);
            return false;
        }
        for (std::uint32_t i = 0; i < req.count; ++i) {
            v4l2_buffer buf{};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) == -1) {
                error = os_error("cannot map the capture buffers of " + path);
                return false;
            }
            Buffer b;
            b.length = buf.length;
            b.start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
            buffers_.push_back(b);
            if (b.start == MAP_FAILED || xioctl(fd_, VIDIOC_QBUF, &buf) == -1) {
                error = os_error("cannot map the capture buffers of " + path);
                return false;
            }
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd_, VIDIOC_STREAMON, &type) == -1) {
            error = os_error("cannot start " + path);
            return false;
        }
        return true;
    }

    bool next(CameraImage& out, std::string& error) override {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        timeval tv{1, 0};
        const int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0) {
            error = r == 0 ? "the camera sends no pictures" : os_error("camera");
            return false;
        }
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_DQBUF, &buf) == -1) {
            error = errno == EAGAIN ? "no picture yet" : os_error("camera");
            return false;
        }
        bool ok = false;
        if (buf.index < buffers_.size() && (buf.flags & V4L2_BUF_FLAG_ERROR) == 0) {
            const auto* data = static_cast<const std::uint8_t*>(buffers_[buf.index].start);
            const std::size_t used = std::min<std::size_t>(buf.bytesused, buffers_[buf.index].length);
            if (pixelformat_ == V4L2_PIX_FMT_YUYV) {
                if (used >= stride_ * static_cast<std::size_t>(height_)) {
                    out.width = width_;
                    out.height = height_;
                    yuyv_to_rgba(data, width_, height_, stride_, out.rgba);
                    ok = true;
                }
            } else {
                ok = decode_mjpeg(std::span<const std::uint8_t>(data, used), out);
            }
        }
        (void)xioctl(fd_, VIDIOC_QBUF, &buf);
        if (!ok) error = "a damaged picture";
        return ok;
    }

    std::string format() const override {
        const char* f = pixelformat_ == V4L2_PIX_FMT_YUYV ? "YUYV" : "MJPEG";
        return std::string(f) + " " + std::to_string(width_) + "x" + std::to_string(height_);
    }

private:
    struct Buffer {
        void* start = MAP_FAILED;
        std::size_t length = 0;
    };
    int fd_ = -1;
    std::uint32_t pixelformat_ = 0;
    int width_ = 0, height_ = 0;
    std::size_t stride_ = 0;
    std::vector<Buffer> buffers_;
};

class V4l2Backend final : public CameraBackend {
public:
    std::string name() const override { return "V4L2"; }

    std::vector<CameraInfo> cameras() override {
        std::vector<CameraInfo> out;
        std::error_code ec;
        for (const fs::directory_entry& e : fs::directory_iterator("/dev", ec)) {
            const std::string n = e.path().filename().string();
            if (n.rfind("video", 0) != 0) continue;
            const int fd = ::open(e.path().c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            std::string card;
            if (can_capture(fd, &card)) out.push_back(CameraInfo{e.path().string(), card + " (" + n + ")"});
            close(fd);
        }
        std::sort(out.begin(), out.end(), [](const CameraInfo& a, const CameraInfo& b) { return a.id < b.id; });
        return out;
    }

    std::unique_ptr<CameraStream> open(const std::string& id, std::string& error) override {
        auto s = std::make_unique<V4l2Stream>();
        if (!s->open_device(id, error)) return nullptr;
        return s;
    }
};

}  // namespace

std::shared_ptr<CameraBackend> make_system_camera_backend() { return std::make_shared<V4l2Backend>(); }

}  // namespace yantra::app
