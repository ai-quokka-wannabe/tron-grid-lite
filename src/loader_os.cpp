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

#include "loader_os.hpp"

#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace LoaderOs
{

    std::string lastLoaderError()
    {
#if defined(_WIN32)
        const DWORD code{GetLastError()};

        char* buffer{nullptr};
        const DWORD length{FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<char*>(&buffer), 0u, nullptr)};

        std::string message{(length > 0u) && (buffer != nullptr) ? std::string{buffer, length} : std::string{"unknown error"}};
        if (buffer != nullptr) {
            LocalFree(buffer);
        }

        // FormatMessage ends its text with CRLF, which reads badly in the middle of a sentence.
        while ((!message.empty()) && ((message.back() == '\n') || (message.back() == '\r'))) {
            message.pop_back();
        }

        return message + " (" + std::to_string(code) + ")";
#else
        const char* message{dlerror()};
        return (message != nullptr) ? std::string{message} : std::string{"unknown error"};
#endif
    }

    void* openLibrary(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        /*
            Windows answers a malformed library with a modal message box before it answers the
            caller. Nobody is necessarily there to dismiss it: a library is loaded during a
            headless run, and on a build machine or an unattended overnight run the box turns a
            clean refusal into a process that waits for ever. That is the same trade this
            repository already refuses around std::abort and the CRT's termination dialog — a
            failure somebody can read beats a hang nobody can see.

            SetThreadErrorMode rather than SetErrorMode because the latter is process-wide, and
            the loader has no business changing how the rest of the Grid reports disk errors.
            The last error is captured before the mode is restored, since it is what the refusal
            will quote.
        */
        DWORD previous_mode{0u};
        const BOOL mode_changed{SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous_mode)};

        HMODULE module{LoadLibraryW(path.c_str())};
        const DWORD load_error{GetLastError()};

        if (mode_changed != FALSE) {
            static_cast<void>(SetThreadErrorMode(previous_mode, nullptr));
        }

        SetLastError(load_error);
        return static_cast<void*>(module);
#else
        /*
            RTLD_NOW rather than lazy, because a library with an unresolved symbol should be
            refused while there is still somebody to tell. Bound lazily it loads, passes every
            check, and dies at the first call instead — inside code the Grid cannot catch.

            RTLD_LOCAL so that one library's symbols are not visible to the next. Two libraries
            from different authors will one day define the same helper name, and with global
            binding the second would silently call the first one's.
        */
        return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }

    void closeLibrary(void* handle) noexcept
    {
#if defined(_WIN32)
        static_cast<void>(FreeLibrary(static_cast<HMODULE>(handle)));
#else
        static_cast<void>(dlclose(handle));
#endif
    }

    void* findSymbol(void* handle, const char* name) noexcept
    {
#if defined(_WIN32)
        const FARPROC symbol{GetProcAddress(static_cast<HMODULE>(handle), name)};
        void* address{nullptr};
        static_assert(sizeof(address) == sizeof(symbol), "a data pointer must hold a FARPROC on this platform");
        std::memcpy(&address, &symbol, sizeof(address));
        return address;
#else
        return dlsym(handle, name);
#endif
    }

    std::filesystem::path executablePath()
    {
#if defined(_WIN32)
        /*
            The buffer grows rather than assuming MAX_PATH. A path longer than 260 characters is
            ordinary on a build agent, and GetModuleFileNameW's answer to a short buffer is to
            truncate, set ERROR_INSUFFICIENT_BUFFER and still report success on older Windows — so a
            single call with a fixed buffer can return a path that exists and is the wrong one.
        */
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;) {
            SetLastError(ERROR_SUCCESS);
            const DWORD length{GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()))};

            if (length == 0u) {
                throw std::runtime_error{"Cannot determine where this executable is: " + lastLoaderError()};
            }

            if ((length < buffer.size()) && (GetLastError() != ERROR_INSUFFICIENT_BUFFER)) {
                return std::filesystem::path{std::wstring{buffer.data(), length}};
            }

            if (buffer.size() >= 65536u) {
                throw std::runtime_error{"This executable's path is implausibly long."};
            }

            buffer.resize(buffer.size() * 2u);
        }
#else
        std::vector<char> buffer(1024);
        for (;;) {
            const ssize_t length{readlink("/proc/self/exe", buffer.data(), buffer.size())};
            if (length < 0) {
                throw std::runtime_error{"Cannot determine where this executable is: /proc/self/exe is unreadable."};
            }

            // readlink neither terminates nor reports truncation, so a full buffer is ambiguous
            // between an exact fit and a path that was cut short. Grow and ask again.
            if (static_cast<size_t>(length) < buffer.size()) {
                return std::filesystem::path{std::string{buffer.data(), static_cast<size_t>(length)}};
            }

            if (buffer.size() >= 65536u) {
                throw std::runtime_error{"This executable's path is implausibly long."};
            }

            buffer.resize(buffer.size() * 2u);
        }
#endif
    }

}
