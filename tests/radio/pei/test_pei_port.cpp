// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The terminal plugin's serial-port layer on its own (RAD-80, RAD-04):
// the pure helpers (Win32 device names, USB ids of Windows instance ids,
// the port list variable), the platform code on paths that are not serial
// ports or not there, and a round trip through the platform's port code
// to the simulated terminal (a pseudo-terminal on Linux; on Windows a
// named pipe, through the same CreateFileW/overlapped I/O code as a COM
// port) with unplug and replug. A real COM port or USB adapter is a
// hardware check (DEVICES.md).
#include "pei_port.h"
#include "pei_sim.hpp"

#include <yantra/gen/scenario.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

using namespace yantra;

namespace {

std::string win32_name(const char* path, int* kind = nullptr)
{
    char out[PEI_PORT_PATH_MAX];
    const int k = pei_port_win32_name(path, out, sizeof out);
    if (kind != nullptr)
        *kind = k;
    return out;
}

struct Usb {
    int ok = 0;
    unsigned vid = 0, pid = 0;
    std::string serial;
};

Usb usb(const char* id)
{
    Usb u;
    char serial[64];
    u.ok = pei_port_parse_usb_ids(id, &u.vid, &u.pid, serial, sizeof serial);
    u.serial = serial;
    return u;
}

// Reads until `text` arrived or the guard ran out.
std::string read_until(pei_port* port, const std::string& text, double seconds = 5.0)
{
    std::string got;
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                            std::chrono::duration<double>(seconds * YANTRA_TEST_TIME_SCALE));
    char buf[256];
    while (got.find(text) == std::string::npos && std::chrono::steady_clock::now() < end) {
        const long n = pei_port_read(port, buf, sizeof buf, 50);
        if (n < 0)
            break;
        got.append(buf, static_cast<std::size_t>(n));
    }
    return got;
}

} // namespace

TEST_CASE("pei port: Win32 device names of COM ports and pipes")
{
    int kind = -1;
    CHECK(win32_name("COM3", &kind) == "\\\\.\\COM3");
    CHECK(kind == PEI_PORT_KIND_COM);
    CHECK(win32_name("com10", &kind) == "\\\\.\\COM10");
    CHECK(kind == PEI_PORT_KIND_COM);
    CHECK(win32_name("\\\\.\\COM256", &kind) == "\\\\.\\COM256");
    CHECK(kind == PEI_PORT_KIND_COM);
    for (const char* other : {"COM0", "COM257", "COM3x", "COM", "COM03", "/dev/ttyUSB0", "\\\\.\\NUL", ""}) {
        INFO(other);
        CHECK(win32_name(other, &kind) == other);
        CHECK(kind == PEI_PORT_KIND_OTHER);
    }
    CHECK(win32_name("\\\\.\\pipe\\yantra-pei-1", &kind) == "\\\\.\\pipe\\yantra-pei-1");
    CHECK(kind == PEI_PORT_KIND_PIPE);
    win32_name("\\\\.\\pipe\\", &kind);
    CHECK(kind == PEI_PORT_KIND_OTHER);
}

TEST_CASE("pei port: USB ids and serial numbers of Windows instance ids")
{
    Usb u = usb("USB\\VID_0403&PID_6001\\A9XYZ12");
    CHECK(u.ok == 1);
    CHECK(u.vid == 0x0403);
    CHECK(u.pid == 0x6001);
    CHECK(u.serial == "A9XYZ12");
    u = usb("FTDIBUS\\VID_0403+PID_6015+DN05ABCDA\\0000");
    CHECK(u.ok == 1);
    CHECK(u.pid == 0x6015);
    CHECK(u.serial == "DN05ABCD"); // the port letter A is not part of it
    u = usb("USB\\VID_1A86&PID_7523\\5&2C1D3B4&0&3"); // no serial: a made-up id
    CHECK(u.ok == 1);
    CHECK(u.vid == 0x1a86);
    CHECK(u.serial.empty());
    u = usb("USB\\VID_22B8&PID_2A62&MI_02\\6&1A2B3C&0&0002"); // a function of a composite device
    CHECK(u.ok == 1);
    CHECK(u.pid == 0x2a62);
    CHECK(u.serial.empty());
    u = usb("ACPI\\PNP0501\\1"); // a built-in COM port
    CHECK(u.ok == 0);
    CHECK(u.vid == 0);
    CHECK(u.serial.empty());
    u = usb("USB\\VID_12&PID_34\\X");
    CHECK(u.ok == 0);
}

TEST_CASE("pei port: the extra port list and the baud rates")
{
    std::vector<std::string> got;
    const std::string sep(1, PEI_PORTS_SEPARATOR);
    const std::string list = "a" + sep + sep + "b c" + sep + std::string(PEI_PORT_PATH_MAX, 'x') + sep + "d";
    pei_port_split_list(
        list.c_str(), [](void* user, const char* p) { static_cast<std::vector<std::string>*>(user)->push_back(p); },
        &got);
    CHECK(got == std::vector<std::string>{"a", "b c", "d"});
    CHECK(pei_baud_supported(38400));
    CHECK(pei_baud_supported(115200));
    CHECK_FALSE(pei_baud_supported(12345));
}

TEST_CASE("pei port: paths that are not there or not serial ports")
{
#ifdef _WIN32
    const char* absent = "COM250";
    const char* not_serial = "\\\\.\\NUL";
#else
    const char* absent = "/dev/yantra-no-such-port";
    const char* not_serial = "/dev/null";
#endif
    char why[256];
    CHECK(pei_port_check(absent, why, sizeof why) == 0);
    CHECK(pei_port_held_elsewhere(absent) == 0);
    int in_use = -1;
    char error[256] = "";
    CHECK(pei_port_open(absent, 38400, &in_use, error, sizeof error) == nullptr);
    CHECK(in_use == 0);
    CHECK(std::string(error).find(absent) != std::string::npos);
    CHECK(pei_port_open(not_serial, 38400, &in_use, error, sizeof error) == nullptr);
    CHECK(in_use == 0);
    CHECK(std::string(error).find("not a serial port") != std::string::npos);
    CHECK(pei_port_open(not_serial, 12345, &in_use, error, sizeof error) == nullptr);
    CHECK(std::string(error).find("baud") != std::string::npos);
    // the list opens nothing and names what open takes
    std::vector<std::string> ports;
    const int n = pei_port_list(
        nullptr,
        [](void* user, const pei_port_info* info) {
            static_cast<std::vector<std::string>*>(user)->push_back(info->path);
            CHECK(info->device[0] != '\0');
        },
        &ports);
    CHECK(n == static_cast<int>(ports.size()));
#ifdef _WIN32
    for (const std::string& p : ports) {
        int kind = -1;
        win32_name(p.c_str(), &kind);
        CHECK_MESSAGE(kind == PEI_PORT_KIND_COM, p);
    }
#endif
}

TEST_CASE("pei port: a round trip to the simulated terminal, unplug and replug")
{
    pei_sim::Terminal terminal(gen::load_scenario_file(std::string(YANTRA_TEST_DATA_DIR) + "/pei/terminal_cells.json"));
    const std::string name = pei_sim::make_port_name("port");
    pei_sim::PortTerminal fake(terminal, name);
    char why[256];
    CHECK(pei_port_check(name.c_str(), why, sizeof why) == 1);
    std::vector<std::string> listed;
    pei_port_list(
        name.c_str(), [](void* user, const pei_port_info* info) {
            static_cast<std::vector<std::string>*>(user)->push_back(info->path);
        },
        &listed);
    CHECK(std::find(listed.begin(), listed.end(), name) != listed.end());

    int in_use = -1;
    char error[256] = "";
    pei_port* port = pei_port_open(name.c_str(), 38400, &in_use, error, sizeof error);
    REQUIRE_MESSAGE(port != nullptr, error);
    CHECK(in_use == 0);
    // a second open of the same port: in use (this program holds it)
    pei_port* second = pei_port_open(name.c_str(), 38400, &in_use, error, sizeof error);
    CHECK(second == nullptr);
    CHECK(in_use == 1);
    CHECK(std::string(error).find("in use") != std::string::npos);
    CHECK(pei_port_held_elsewhere(name.c_str()) == 1);

    const std::string cmd = "AT+GMI\r";
    REQUIRE(pei_port_write(port, cmd.data(), cmd.size()));
    CHECK(read_until(port, "OK").find(pei_sim::kManufacturer) != std::string::npos);
    pei_port_flush_input(port); // nothing waiting: harmless
    const std::string model = "AT+GMM\r";
    REQUIRE(pei_port_write(port, model.data(), model.size()));
    CHECK(read_until(port, "OK").find(pei_sim::kModel) != std::string::npos);
    char buf[64];
    CHECK(pei_port_read(port, buf, sizeof buf, 0) >= 0); // nothing waiting: no error

    fake.unplug();
    long r = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((r = pei_port_read(port, buf, sizeof buf, 50)) >= 0 && std::chrono::steady_clock::now() < end) {
    }
    CHECK(r == -1);
    CHECK(std::string(pei_port_error(port)).size() > 0);
    CHECK(pei_port_check(name.c_str(), why, sizeof why) == 0);
    pei_port_close(port);

    fake.replug();
    CHECK(pei_port_check(name.c_str(), why, sizeof why) == 1);
    port = pei_port_open(name.c_str(), 115200, &in_use, error, sizeof error);
    REQUIRE_MESSAGE(port != nullptr, error);
    REQUIRE(pei_port_write(port, model.data(), model.size()));
    CHECK(read_until(port, "OK").find(pei_sim::kModel) != std::string::npos);
    pei_port_close(port);
    CHECK(pei_port_held_elsewhere(name.c_str()) == 0);
}
