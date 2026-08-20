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

#include <cstring>
#include <filesystem>
#include <string>

/*!
    The operating system's half of loading a shared library, shared by every loader the Grid has:
    Programs and Link are different beings with different contracts, but LoadLibrary's modal
    failure box, dlopen's binding flags and GetModuleFileNameW's truncation behaviour are facts
    about the platform, not about either of them. Plain names, because these are events in the
    operating system rather than on the Grid.
*/
namespace LoaderOs
{

    //! The last loader error, formatted for a refusal message a human will read.
    [[nodiscard]] std::string lastLoaderError();

    /*!
        Opens a shared library, with the platform's failure modes tamed: on Windows the modal
        "Bad Image" box is suppressed for this thread only (a headless run has nobody to dismiss
        it, and the rest of the process's error reporting is not the loader's to change); on
        Linux the binding is RTLD_NOW | RTLD_LOCAL, so an unresolved symbol is refused while
        there is still somebody to tell and one library's symbols are not injected into the
        next one's lookups. Returns null on failure with lastLoaderError() describing why.
    */
    [[nodiscard]] void* openLibrary(const std::filesystem::path& path);

    void closeLibrary(void* handle) noexcept;

    //! The raw address of a named export, or null. Callers convert to their own function type.
    [[nodiscard]] void* findSymbol(void* handle, const char* name) noexcept;

    /*!
        A named export as a function pointer. Copied rather than cast: ISO C++ does not allow
        converting between an object pointer and a function pointer, and -Wpedantic says so under
        -Werror; POSIX guarantees the round trip works, so the bits are moved without asking the
        type system to bless it.
    */
    template <typename FunctionPointer> [[nodiscard]] FunctionPointer findEntryPoint(void* handle, const char* name) noexcept
    {
        void* const symbol{findSymbol(handle, name)};
        if (symbol == nullptr) {
            return nullptr;
        }

        FunctionPointer entry_point{nullptr};
        std::memcpy(&entry_point, &symbol, sizeof(entry_point));
        return entry_point;
    }

    //! Full path of the running executable. Throws std::runtime_error when the platform will
    //! not say, because everything resolved relative to the executable is resolved from this.
    [[nodiscard]] std::filesystem::path executablePath();

}
