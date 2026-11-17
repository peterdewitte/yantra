// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47, spec 16.2: Windows cameras through Media Foundation, Yantra's
// own code (no camera library). The video capture devices come from
// MFEnumDeviceSources; a camera is read with a synchronous source reader
// whose video processing converts every camera format to RGB32 (BGRX), at
// the device's preferred size, which this code turns into RGBA rows.
// Media Foundation is started once per stream (MFStartup/MFShutdown are
// reference counted) on the worker thread, which joins the multithreaded
// COM apartment.

#include "yantra/app/camera.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace yantra::app {

namespace {

template <class T>
void release(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

std::string utf8(const wchar_t* w, int len = -1) {
    if (w == nullptr) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len < 0 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

std::string hresult(const char* what, HRESULT hr) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(what) + " (" + buf + ")";
}

// COM and Media Foundation for the calling thread, for as long as it lives.
class MfSession {
public:
    MfSession() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_ = SUCCEEDED(co);  // S_FALSE: already initialised here, still counted
        ok_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~MfSession() {
        if (ok_) MFShutdown();
        if (com_) CoUninitialize();
    }
    MfSession(const MfSession&) = delete;
    MfSession& operator=(const MfSession&) = delete;
    bool ok() const { return ok_; }

private:
    bool com_ = false;
    bool ok_ = false;
};

// The capture devices: Activate objects; the caller releases them and
// frees the array with CoTaskMemFree.
HRESULT enumerate(IMFActivate*** devices, UINT32* count) {
    IMFAttributes* attr = nullptr;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr)) hr = attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attr, devices, count);
    release(attr);
    return hr;
}

std::string string_attribute(IMFActivate* a, const GUID& key) {
    WCHAR* value = nullptr;
    UINT32 len = 0;
    if (FAILED(a->GetAllocatedString(key, &value, &len)) || value == nullptr) return {};
    std::string s = utf8(value, static_cast<int>(len));
    CoTaskMemFree(value);
    return s;
}

class MfStream final : public CameraStream {
public:
    ~MfStream() override {
        release(reader_);
        if (source_ != nullptr) source_->Shutdown();
        release(source_);
    }

    bool open_device(const std::string& id, std::string& error) {
        if (!session_.ok()) {
            error = "Media Foundation is not available";
            return false;
        }
        // The device by its symbolic link.
        IMFAttributes* attr = nullptr;
        HRESULT hr = MFCreateAttributes(&attr, 2);
        const std::wstring link = wide(id);
        if (SUCCEEDED(hr))
            hr = attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr)) hr = attr->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link.c_str());
        if (SUCCEEDED(hr)) hr = MFCreateDeviceSource(attr, &source_);
        release(attr);
        if (FAILED(hr)) {
            error = hresult("the camera cannot be opened", hr);
            return false;
        }
        // A source reader that converts to RGB32.
        IMFAttributes* rattr = nullptr;
        hr = MFCreateAttributes(&rattr, 1);
        if (SUCCEEDED(hr)) hr = rattr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source_, rattr, &reader_);
        release(rattr);
        if (FAILED(hr)) {
            error = hresult("the camera cannot be read", hr);
            return false;
        }
        const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        IMFMediaType* type = nullptr;
        hr = MFCreateMediaType(&type);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = reader_->SetCurrentMediaType(stream, nullptr, type);
        release(type);
        if (FAILED(hr)) {
            error = hresult("the camera gives no pictures Yantra can convert", hr);
            return false;
        }
        IMFMediaType* current = nullptr;
        hr = reader_->GetCurrentMediaType(stream, &current);
        if (SUCCEEDED(hr)) {
            UINT32 w = 0, h = 0;
            hr = MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
            width_ = static_cast<int>(w);
            height_ = static_cast<int>(h);
            UINT32 stride = 0;
            if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)))
                stride_ = static_cast<LONG>(stride);
            else
                stride_ = static_cast<LONG>(w) * 4;
        }
        release(current);
        if (FAILED(hr) || width_ <= 0 || height_ <= 0) {
            error = hresult("the camera's picture size is unknown", hr);
            return false;
        }
        return true;
    }

    bool next(CameraImage& out, std::string& error) override {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        // Synchronous: waits for the next frame.
        HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags,
                                         nullptr, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR) != 0 || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            release(sample);
            error = hresult("the camera stopped", hr);
            return false;
        }
        if (sample == nullptr) {  // a stream tick, no picture
            error = "no picture yet";
            return false;
        }
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        bool ok = false;
        if (SUCCEEDED(hr)) {
            BYTE* data = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &len))) {
                const LONG pitch = stride_ < 0 ? -stride_ : stride_;
                if (pitch >= width_ * 4 && static_cast<std::uint64_t>(len) >= static_cast<std::uint64_t>(pitch) * static_cast<std::uint64_t>(height_)) {
                    out.width = width_;
                    out.height = height_;
                    out.rgba.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u);
                    for (int y = 0; y < height_; ++y) {
                        // A negative stride: the rows are stored bottom first.
                        const int src_row = stride_ < 0 ? height_ - 1 - y : y;
                        const BYTE* s = data + static_cast<std::size_t>(src_row) * static_cast<std::size_t>(pitch);
                        std::uint8_t* d = out.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4u;
                        for (int x = 0; x < width_; ++x) {  // BGRX to RGBA
                            d[4 * x + 0] = s[4 * x + 2];
                            d[4 * x + 1] = s[4 * x + 1];
                            d[4 * x + 2] = s[4 * x + 0];
                            d[4 * x + 3] = 255;
                        }
                    }
                    ok = true;
                }
                buffer->Unlock();
            }
        }
        release(buffer);
        release(sample);
        if (!ok) error = "a damaged picture";
        return ok;
    }

    std::string format() const override { return "RGB32 " + std::to_string(width_) + "x" + std::to_string(height_); }

private:
    MfSession session_;
    IMFMediaSource* source_ = nullptr;
    IMFSourceReader* reader_ = nullptr;
    int width_ = 0, height_ = 0;
    LONG stride_ = 0;
};

class MfBackend final : public CameraBackend {
public:
    std::string name() const override { return "Media Foundation"; }

    std::vector<CameraInfo> cameras() override {
        std::vector<CameraInfo> out;
        MfSession session;
        if (!session.ok()) return out;
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (FAILED(enumerate(&devices, &count))) return out;
        for (UINT32 i = 0; i < count; ++i) {
            CameraInfo c;
            c.id = string_attribute(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            c.name = string_attribute(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            if (c.name.empty()) c.name = "Camera " + std::to_string(i + 1);
            if (!c.id.empty()) out.push_back(std::move(c));
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
        return out;
    }

    std::unique_ptr<CameraStream> open(const std::string& id, std::string& error) override {
        auto s = std::make_unique<MfStream>();
        if (!s->open_device(id, error)) return nullptr;
        return s;
    }
};

}  // namespace

std::shared_ptr<CameraBackend> make_system_camera_backend() { return std::make_shared<MfBackend>(); }

}  // namespace yantra::app
