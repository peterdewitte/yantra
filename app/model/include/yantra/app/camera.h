// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47 (and IND-48): photos for the project, from a file or taken with
// a camera. Spec 16.2: no camera library; Yantra's own capture code over
// the operating system's interfaces:
//
// - Linux: V4L2 (camera_v4l2.cpp): the /dev/video* devices that capture
//   video, MJPEG frames (decoded with stb_image) or YUYV frames
//   (converted by yuyv_to_rgba), memory-mapped streaming.
// - Windows: Media Foundation (camera_mf.cpp): the video capture devices
//   (MFEnumDeviceSources), a source reader with video processing that
//   delivers RGB32 frames.
// - The fake camera (tests, screenshots): YANTRA_FAKE_CAMERA=1 gives one
//   camera with a generated test image (fake_camera_image); "none" gives
//   a system without cameras.
//
// The photo source is one small interface (PhotoSource, CameraCapture,
// photo_from_file) that the About building tab and the plan notes share.
// Nothing leaves the computer: a photo goes into the project file only.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace yantra::app {

struct CameraInfo {
    std::string id;    ///< device path (Linux) or symbolic link (Windows); "fake" for the fake camera
    std::string name;  ///< what the device calls itself
};

/// A frame: RGBA rows, top first.
struct CameraImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool empty() const { return width <= 0 || height <= 0; }
};

/// An opened camera.
class CameraStream {
public:
    virtual ~CameraStream() = default;
    /// The next frame (waits at most about a second). False with `error`
    /// set when none came (unplugged, busy, timeout).
    virtual bool next(CameraImage& out, std::string& error) = 0;
    /// "MJPEG 1280x720", "YUYV 640x480", "RGB32 1280x720", ...
    virtual std::string format() const = 0;
};

class CameraBackend {
public:
    virtual ~CameraBackend() = default;
    virtual std::string name() const = 0;  ///< "V4L2", "Media Foundation", "fake", "none"
    virtual std::vector<CameraInfo> cameras() = 0;
    /// nullptr with `error` set when it cannot be opened.
    virtual std::unique_ptr<CameraStream> open(const std::string& id, std::string& error) = 0;
};

/// The operating system's cameras (V4L2 on Linux, Media Foundation on
/// Windows; elsewhere a backend without cameras).
std::shared_ptr<CameraBackend> make_system_camera_backend();
/// One fake camera with the generated test image.
std::shared_ptr<CameraBackend> make_fake_camera_backend();
/// A backend without cameras.
std::shared_ptr<CameraBackend> make_no_camera_backend();
/// YANTRA_FAKE_CAMERA: "1" (or any other value) the fake camera, "none" no
/// camera; unset or empty: the system's cameras.
std::shared_ptr<CameraBackend> make_camera_backend();

/// YUYV (YUY2, 4:2:2: Y0 U Y1 V per two pixels, BT.601 limited range) to
/// RGBA. `stride` is the bytes per source row (>= 2 * width); an odd width
/// uses the last pair's chroma.
void yuyv_to_rgba(const std::uint8_t* src, int width, int height, std::size_t stride, std::vector<std::uint8_t>& rgba);
/// A camera's MJPEG frame (a JPEG, possibly without Huffman tables, which
/// stb_image fills in) to RGBA; false when it is not decodable.
bool decode_mjpeg(std::span<const std::uint8_t> jpeg, CameraImage& out);
/// The fake camera's test image: colour bars, a grey ramp and a frame
/// counter square that moves with `frame`.
CameraImage fake_camera_image(int width, int height, std::uint64_t frame);

/// A photo for the project: the bytes of an image file.
struct Photo {
    std::vector<std::uint8_t> bytes;
    std::string name;    ///< file name; "camera.jpg" for a camera photo
    std::string source;  ///< "file" or "camera"
};

/// The photo source both IND-47 and IND-48 use.
class PhotoSource {
public:
    /// A photo from an image file: its bytes as they are (any stb_image
    /// format, at most project::kMaxPhotoBytes); nullopt with `error` set
    /// when it is not such an image.
    static std::optional<Photo> from_file(const std::filesystem::path& file, std::string& error);
    /// A camera frame as a JPEG (quality 90).
    static Photo from_camera(const CameraImage& frame);
};

/// The camera window's capture: a worker reads frames for the preview;
/// take() keeps the current one.
class CameraCapture {
public:
    enum class State { Idle, Starting, Running, Failed };
    explicit CameraCapture(std::shared_ptr<CameraBackend> backend);
    ~CameraCapture();
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    const std::shared_ptr<CameraBackend>& backend() const { return backend_; }
    /// The cameras now (enumerated again on each call).
    std::vector<CameraInfo> cameras();
    /// Opens a camera and starts the preview (stops a running one).
    void start(const std::string& id);
    void stop();
    State state() const { return static_cast<State>(state_.load()); }
    std::string error() const;
    std::string format() const;
    /// The number of frames so far; copies the latest into `out` when it
    /// is not null and a frame came.
    std::uint64_t frame(CameraImage* out) const;
    /// The latest frame as a photo; nullopt before the first frame.
    std::optional<Photo> take() const;

private:
    void run(std::string id);
    std::shared_ptr<CameraBackend> backend_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<int> state_{0};
    mutable std::mutex mutex_;
    std::string error_, format_;
    CameraImage latest_;
    std::uint64_t frames_ = 0;
};

}  // namespace yantra::app
