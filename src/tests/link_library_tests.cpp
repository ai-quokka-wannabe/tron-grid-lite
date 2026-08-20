/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify it under the terms of
    the GNU General Public License as published by the Free Software Foundation, either version
    3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along with this program.
    If not, see https://www.gnu.org/licenses/.
*/

/*
    The Grid consuming Link for the first time. Merely compiling this file is half the etape:
    lnk_protocol.h and lnk_client.h are here compiled by a C++ toolchain for the first time, so
    their static asserts finally run — on every preset, under /WX and -Werror. The other half is
    below: the real library, built by cargo out of the submodule, is loaded exactly as a Program
    is loaded, and everything it says about itself is checked against what this Grid was
    compiled to expect.

    The assertions match refusal text rather than merely catching, because "it threw" cannot
    tell a refusal from an accident.
*/

#include "../link_library.hpp"
#include "../loader_os.hpp"

#include <lnk/lnk_client.h>
#include <lnk/lnk_protocol.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <testing/testing.hpp>

namespace
{

    [[nodiscard]] std::filesystem::path libraryFile()
    {
        return std::filesystem::path{TGL_LINK_LIBRARY_FILE};
    }

    /*
        The fingerprint the submodule records beside its header, parsed from the same file the
        library itself compiles in. The library's protocol_fingerprint() answering these bytes
        is the three-way pin: the file, the Rust build and this C++ reading of it agree, or this
        test names which one wandered.
    */
    [[nodiscard]] std::array<std::uint8_t, 32> recordedFingerprint()
    {
        std::ifstream file{std::filesystem::path{TGL_LINK_FINGERPRINT_FILE}};
        if (!file.good()) {
            throw std::runtime_error{"the recorded fingerprint file is missing; the submodule is not initialised"};
        }

        std::string line{};
        std::string hex{};
        while (std::getline(file, line)) {
            if (line.rfind("sha256=", 0u) == 0u) {
                hex = line.substr(7u);
                break;
            }
        }
        if (hex.size() != 64u) {
            throw std::runtime_error{"the recorded fingerprint is not 64 hex characters"};
        }

        std::array<std::uint8_t, 32> fingerprint{};
        for (std::size_t index = 0u; index < fingerprint.size(); ++index) {
            fingerprint[index] = static_cast<std::uint8_t>(std::stoul(hex.substr(index * 2u, 2u), nullptr, 16));
        }
        return fingerprint;
    }

}

TEST_CASE(the_library_loads_and_agrees_with_the_header_about_everything)
{
    const LinkLib::Library link{libraryFile()};
    const LnkClientVTable& vtable{link.vtable()};

    TEST_CHECK_EQUAL(vtable.vtable_bytes, static_cast<std::uint32_t>(sizeof(LnkClientVTable)));
    TEST_CHECK_EQUAL(vtable.abi_version, static_cast<std::uint32_t>(LNK_CLIENT_ABI_VERSION));
    TEST_CHECK_EQUAL(vtable.protocol_version(), static_cast<std::uint32_t>(LNK_PROTOCOL_VERSION));

    std::array<std::uint8_t, 32> fingerprint{};
    vtable.protocol_fingerprint(fingerprint.data());
    TEST_CHECK(fingerprint == recordedFingerprint());
}

TEST_CASE(a_version_the_library_cannot_satisfy_is_refused_by_name)
{
    try {
        const LinkLib::Library link{libraryFile(), 999u};
        TEST_CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message{error.what()};
        TEST_CHECK(message.find("refused client ABI version 999") != std::string::npos);
        TEST_CHECK(message.find("lnk_client.h") != std::string::npos);
    }
}

TEST_CASE(a_library_that_is_not_there_is_a_refusal_not_a_crash)
{
    try {
        const LinkLib::Library link{libraryFile().parent_path() / "no_such_link.dll"};
        TEST_CHECK(false);
    } catch (const std::runtime_error& error) {
        const std::string message{error.what()};
        TEST_CHECK(message.find("could not be loaded") != std::string::npos);
        TEST_CHECK(message.find("no_such_link") != std::string::npos);
    }
}

TEST_CASE(link_lives_beside_the_executable_and_nowhere_else)
{
    /*
        The residence rule, owner-stated: Link's library is required to sit beside the Grid's
        own executable, with no path flag and no search order. The resolver must therefore
        answer exactly this directory and the platform's name — and the copy the build placed
        here must genuinely load, which is the rule exercised end to end rather than recited.
    */
    const std::filesystem::path beside{LinkLib::Library::besideExecutable()};
    TEST_CHECK(beside.parent_path() == LoaderOs::executablePath().parent_path());
#if defined(_WIN32)
    TEST_CHECK(beside.filename() == "link.dll");
#else
    TEST_CHECK(beside.filename() == "liblink.so");
#endif

    const LinkLib::Library link{beside};
    TEST_CHECK_EQUAL(link.vtable().protocol_version(), static_cast<std::uint32_t>(LNK_PROTOCOL_VERSION));
}

TEST_CASE(connect_refuses_a_null_address_through_the_loaded_table)
{
    /*
        One call through a loaded function pointer, so the test proves calls cross the boundary
        and come back — not only that the table's numbers read correctly. A null address is the
        cheapest honest call: refused inside the library, no socket, no timeout.
    */
    const LinkLib::Library link{libraryFile()};
    const LnkClientVTable& vtable{link.vtable()};

    LnkWelcome welcome{};
    LnkStatus status{-1};
    LnkClient* const client{vtable.connect(nullptr, LNK_ROLE_CREATURE_HOST, 100u, &welcome, &status, nullptr, 0u)};
    TEST_CHECK(client == nullptr);
    TEST_CHECK_EQUAL(status, LNK_BAD_ARGUMENT);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
