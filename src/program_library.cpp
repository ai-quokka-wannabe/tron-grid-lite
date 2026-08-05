/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "program_library.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{

#if defined(_WIN32)
    constexpr const char* LIBRARY_PREFIX{""};
    constexpr const char* LIBRARY_SUFFIX{".dll"};
#else
    constexpr const char* LIBRARY_PREFIX{"lib"};
    constexpr const char* LIBRARY_SUFFIX{".so"};
#endif

    //! The operating system's account of why the last load or lookup failed, as text.
    [[nodiscard]] std::string lastLoaderError()
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

    [[nodiscard]] void* openLibrary(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        /*
            Windows answers a malformed library with a modal message box before it answers the
            caller. Nobody is necessarily there to dismiss it: a Program library is loaded during a
            headless run, and on a build machine or an unattended overnight run the box turns a clean
            refusal into a process that waits for ever. That is the same trade this repository
            already refuses around std::abort and the CRT's termination dialog — a failure somebody
            can read beats a hang nobody can see.

            SetThreadErrorMode rather than SetErrorMode because the latter is process-wide, and the
            loader has no business changing how the rest of the Grid reports disk errors. The last
            error is captured before the mode is restored, since it is what the refusal will quote.
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
            RTLD_NOW rather than lazy, because a Program with an unresolved symbol should be refused
            while there is still somebody to tell. Bound lazily it loads, passes every check here,
            and dies at the first tick instead — inside a call the Grid cannot catch.

            RTLD_LOCAL so that one Program's symbols are not visible to the next. Two Programs from
            different authors will one day define the same helper name, and with global binding the
            second would silently call the first one's.
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

    [[nodiscard]] TglGetProgramVTableFn findEntryPoint(void* handle) noexcept
    {
#if defined(_WIN32)
        const FARPROC symbol{GetProcAddress(static_cast<HMODULE>(handle), "tglGetProgramVTable")};
#else
        void* const symbol{dlsym(handle, "tglGetProgramVTable")};
#endif
        if (symbol == nullptr) {
            return nullptr;
        }

        /*
            Copied rather than cast. ISO C++ does not allow converting between an object pointer and
            a function pointer, and -Wpedantic says so under -Werror; POSIX guarantees the round trip
            works, so the bits are moved without asking the type system to bless it.
        */
        TglGetProgramVTableFn entry_point{nullptr};
        std::memcpy(&entry_point, &symbol, sizeof(entry_point));
        return entry_point;
    }

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
                closeLibrary(m_handle);
            }
        }

        HandleGuard(const HandleGuard&) = delete;
        HandleGuard& operator=(const HandleGuard&) = delete;
        HandleGuard(HandleGuard&&) = delete;
        HandleGuard& operator=(HandleGuard&&) = delete;

        [[nodiscard]] void* get() const noexcept
        {
            return m_handle;
        }

        //! Hands ownership to the caller once every check has passed.
        [[nodiscard]] void* release() noexcept
        {
            void* const handle{m_handle};
            m_handle = nullptr;
            return handle;
        }

    private:
        void* m_handle;
    };

    [[noreturn]] void refuse(std::string_view identifier, const std::string& reason)
    {
        throw std::runtime_error{"Program \"" + std::string{identifier} + "\": " + reason};
    }

} // namespace

namespace ProgramLib
{

    bool identifierIsWellFormed(std::string_view identifier) noexcept
    {
        if (identifier.empty() || (identifier.size() > MAX_IDENTIFIER_LENGTH)) {
            return false;
        }

        const auto isAlphanumeric = [](char c) noexcept {
            return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'));
        };

        // Leading punctuation is excluded so that a name cannot begin with the hyphen that would
        // make it look like a command-line option wherever it is later printed or passed on.
        if (!isAlphanumeric(identifier.front())) {
            return false;
        }

        for (const char c : identifier) {
            if (!isAlphanumeric(c) && (c != '_') && (c != '-')) {
                return false;
            }
        }

        return true;
    }

    std::filesystem::path resolve(const std::filesystem::path& directory, std::string_view identifier)
    {
        if (!identifierIsWellFormed(identifier)) {
            refuse(identifier,
                "not a well-formed identifier. A Program is named rather than pathed: letters, digits, underscore and hyphen only, beginning with a letter or digit, at "
                "most "
                    + std::to_string(MAX_IDENTIFIER_LENGTH) + " characters.");
        }

        return directory / (LIBRARY_PREFIX + std::string{identifier} + LIBRARY_SUFFIX);
    }

    Library::Library(const std::filesystem::path& directory, std::string_view identifier, const TglLibraryInfo& info) :
        m_identifier(identifier)
    {
        const std::filesystem::path path{resolve(directory, identifier)};

        /*
            Asked before loading purely so that the commonest mistake gets the clearest answer. The
            loader would refuse a missing file anyway, but it would do it in the platform's words,
            and "The specified module could not be found" is also what a *present* library with a
            missing dependency reports — which is a different problem entirely.
        */
        std::error_code status;
        if (!std::filesystem::is_regular_file(path, status)) {
            refuse(identifier, "no library at " + path.string());
        }

        HandleGuard handle{openLibrary(path)};
        if (handle.get() == nullptr) {
            refuse(identifier, "the operating system refused to load " + path.string() + ": " + lastLoaderError());
        }

        const TglGetProgramVTableFn entry_point{findEntryPoint(handle.get())};
        if (entry_point == nullptr) {
            refuse(identifier, "exports no tglGetProgramVTable. Every Program library exports exactly that name, with C linkage.");
        }

        const TglProgramVTable* const vtable{entry_point(TGL_ABI_VERSION)};
        if (vtable == nullptr) {
            refuse(identifier, "cannot satisfy ABI version " + std::to_string(TGL_ABI_VERSION) + ", which is the version this Grid was built against.");
        }

        /*
            struct_size is read before anything else in the vtable, because it is what says how much
            of the vtable there is to read. It is first in the struct for ever, and this is the whole
            reason it exists: without it a Grid that grew the vtable would read past the end of an
            older Program's static object and call through whatever its linker placed next, which
            usually appears to work because that memory is usually zero.
        */
        if (vtable->struct_size < TGL_PROGRAM_VTABLE_MIN_SIZE) {
            refuse(identifier,
                "returned a vtable of " + std::to_string(vtable->struct_size) + " bytes, smaller than the " + std::to_string(TGL_PROGRAM_VTABLE_MIN_SIZE)
                    + " that ABI version 1 requires. Initialise it with TGL_PROGRAM_VTABLE_HEADER.");
        }

        /*
            Checked in addition to the argument passed in above, and not instead of it. A Program
            written in C or C++ gets this field from a macro and cannot easily get it wrong, but a
            hand-written binding in another language may ignore the argument entirely and return a
            vtable regardless — and it must still fail here rather than at the first tick.
        */
        if (vtable->abi_version != TGL_ABI_VERSION) {
            refuse(identifier,
                "was built against ABI version " + std::to_string(vtable->abi_version) + " and this Grid speaks version " + std::to_string(TGL_ABI_VERSION)
                    + ". Rebuild the Program against this header.");
        }

        if ((vtable->library_init == nullptr) || (vtable->program_rez == nullptr) || (vtable->program_tick == nullptr) || (vtable->program_derez == nullptr)
            || (vtable->library_shutdown == nullptr)) {
            refuse(identifier, "left a required entry point null. All five are required at ABI version 1.");
        }

        m_vtable = vtable;
        m_handle = handle.release();

        // Last, because until now the library could still be refused, and library_init is the point
        // at which a Program is entitled to assume it has been accepted.
        m_vtable->library_init(&info);
    }

    Library::~Library()
    {
        /*
            A destructor is implicitly noexcept, so anything that escapes here terminates the process
            outright with no handler anywhere able to intervene. Neither call below is meant to throw
            — one is a C function the ABI forbids to, the other is the operating system — but "meant
            to" is not a mechanism, and this is the last thing to run before the library's code is
            unmapped.
        */
        try {
            if (m_vtable != nullptr) {
                m_vtable->library_shutdown();
            }

            if (m_handle != nullptr) {
                closeLibrary(m_handle);
            }
        } catch (...) {
        }
    }

    const TglProgramVTable& Library::vtable() const noexcept
    {
        return *m_vtable;
    }

    const std::string& Library::identifier() const noexcept
    {
        return m_identifier;
    }

} // namespace ProgramLib
