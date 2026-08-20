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

#pragma once

#include <lnk/lnk_client.h>

#include <cstdint>
#include <filesystem>

/*!
    Loading Link — the wire of the Grid — exactly as a Program is loaded: a shared library found
    at run time, one exported symbol behind which everything lives, and a refusal at load rather
    than a corruption at call. Link is the organisation's own library from its own repository,
    but this loader extends it no trust a Program would not get: the symbol may be missing, the
    version may be refused, the table may disagree about its own size, and every one of those is
    a named refusal while there is still somebody to tell.
*/
namespace LinkLib
{

    class Library {
    public:
        /*!
            Where Link is required to live: beside the Grid's own executable, under the
            platform's name for it — and nowhere else. Deliberately no path flag and no search
            order: one place, always, so a stale copy somewhere else can never be the one that
            loads. Programs are named through `--program` precisely because many may be
            installed side by side; there is exactly one Link, and it travels with the
            executable it was built for.
        */
        [[nodiscard]] static std::filesystem::path besideExecutable();

        /*!
            Loads the library at `path`, resolves `lnkGetClientVTable`, and asks it for
            `abi_version` — LNK_CLIENT_ABI_VERSION unless a test is deliberately asking for a
            refusal. Throws std::runtime_error naming the path and the reason on any refusal:
            an unloadable file, a missing symbol, a version the library will not satisfy, or a
            vtable whose own size disagrees with the header this Grid was compiled against.
        */
        explicit Library(const std::filesystem::path& path, std::uint32_t abi_version = LNK_CLIENT_ABI_VERSION);

        ~Library();

        Library(const Library&) = delete;
        Library& operator=(const Library&) = delete;
        Library(Library&&) = delete;
        Library& operator=(Library&&) = delete;

        //! The client surface. Valid for exactly as long as this object lives.
        [[nodiscard]] const LnkClientVTable& vtable() const noexcept
        {
            return *m_vtable;
        }

    private:
        void* m_handle{nullptr};
        const LnkClientVTable* m_vtable{nullptr};
    };

}
