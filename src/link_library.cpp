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

#include "link_library.hpp"

#include "loader_os.hpp"

#include <stdexcept>
#include <string>

namespace
{

    //! Unloads on the way out of a failed constructor, so a refusal does not leak the handle.
    class HandleGuard {
    public:
        explicit HandleGuard(void* handle) noexcept :
            m_handle(handle)
        {
        }

        ~HandleGuard()
        {
            if (m_handle != nullptr) {
                LoaderOs::closeLibrary(m_handle);
            }
        }

        HandleGuard(const HandleGuard&) = delete;
        HandleGuard& operator=(const HandleGuard&) = delete;
        HandleGuard(HandleGuard&&) = delete;
        HandleGuard& operator=(HandleGuard&&) = delete;

        [[nodiscard]] void* release() noexcept
        {
            void* handle{m_handle};
            m_handle = nullptr;
            return handle;
        }

    private:
        void* m_handle{nullptr};
    };

}

namespace LinkLib
{

    std::filesystem::path Library::besideExecutable()
    {
#if defined(_WIN32)
        return LoaderOs::executablePath().parent_path() / "link.dll";
#else
        return LoaderOs::executablePath().parent_path() / "liblink.so";
#endif
    }

    Library::Library(const std::filesystem::path& path, const std::uint32_t abi_version)
    {
        void* const handle{LoaderOs::openLibrary(path)};
        if (handle == nullptr) {
            throw std::runtime_error{"Link library \"" + path.string() + "\" could not be loaded: " + LoaderOs::lastLoaderError()};
        }
        HandleGuard guard{handle};

        const LnkGetClientVTableFn entry_point{LoaderOs::findEntryPoint<LnkGetClientVTableFn>(handle, "lnkGetClientVTable")};
        if (entry_point == nullptr) {
            throw std::runtime_error{
                "Link library \"" + path.string() + "\" exports no lnkGetClientVTable. That symbol is the whole doorway; without it this is not Link."};
        }

        const LnkClientVTable* const vtable{entry_point(abi_version)};
        if (vtable == nullptr) {
            throw std::runtime_error{"Link library \"" + path.string() + "\" refused client ABI version " + std::to_string(abi_version)
                + ". Rebuild the Grid against this Link's lnk_client.h, or this Link against the Grid's."};
        }

        /*
            The table's first member is its own size as the library was compiled, and this Grid
            was compiled against the header's idea of the same number. Disagreement means a stale
            header beside a fresh library or the reverse — the exact skew the check exists for,
            and the one thing the version number alone cannot see.
        */
        if (vtable->vtable_bytes != sizeof(LnkClientVTable)) {
            throw std::runtime_error{"Link library \"" + path.string() + "\" carries a vtable of " + std::to_string(vtable->vtable_bytes)
                + " bytes where this Grid expects " + std::to_string(sizeof(LnkClientVTable))
                + ". The header and the library have drifted apart; rebuild the pair together."};
        }

        m_handle = guard.release();
        m_vtable = vtable;
    }

    Library::~Library()
    {
        if (m_handle != nullptr) {
            LoaderOs::closeLibrary(m_handle);
        }
    }

}
