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

#include "../program_library.hpp"

#include <testing/testing.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/*
    Almost every test here exercises a refusal, and that is the point of the file rather than an
    accident of it. The loader is the first thing in this repository that runs code somebody else
    compiled, so what matters is not that it loads a good Program — one test covers that — but that
    it declines a bad one at the boundary instead of somewhere further in, where the symptom would be
    a call through a null pointer inside a tick.

    Each refusal has a fixture wrong in exactly one way, built beside the good one so that the loader
    reaches them the only way it reaches anything: by resolving an identifier against a directory.
    The assertions match the refusal's text rather than merely catching, because "it threw" cannot
    tell one guard from its neighbour — that lesson is written up in spirv_tests.cpp, where a test
    passed while covering the wrong check entirely.

    No Vulkan, no device, no display. All of it runs under ctest on every push.
*/

namespace
{

    //! Where the build put the Program fixtures. Supplied by CMake; never assembled from a string.
    [[nodiscard]] std::filesystem::path fixtureDirectory()
    {
        return std::filesystem::path{TGL_TEST_PROGRAM_DIR};
    }

    //! Facts a library is handed at init. The values are arbitrary; nothing under test reads them.
    [[nodiscard]] TglLibraryInfo libraryInfo()
    {
        TglLibraryInfo info{};
        info.creature_count = 1u;
        info.nominal_dt_seconds = 0.03125f;
        return info;
    }

    /*!
        Returns the refusal message, or an empty string when the library loaded.

        The message rather than a bare true, for the reason spirv_tests.cpp records: ten guards fire
        in sequence here, several of them on inputs that would also trip the next one along, and a
        test that only asks whether something threw cannot tell which fired.
    */
    [[nodiscard]] std::string refusalFor(std::string_view identifier)
    {
        try {
            const ProgramLib::Library library{fixtureDirectory(), identifier, libraryInfo()};
            static_cast<void>(library.vtable());
            return {};
        } catch (const std::runtime_error& error) {
            return error.what();
        }
    }

    //! Returns true when the refusal message names the expected guard.
    [[nodiscard]] bool refusedBecause(std::string_view identifier, const std::string& expected)
    {
        const std::string message{refusalFor(identifier)};
        return (!message.empty()) && (message.find(expected) != std::string::npos);
    }

    //! A directory that removes itself, for the fixtures a build cannot produce.
    class TemporaryDirectory {
    public:
        explicit TemporaryDirectory(const std::string& name) :
            m_path(std::filesystem::temp_directory_path() / ("tgl_program_test_" + name))
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
            std::filesystem::create_directories(m_path, ignored);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
        TemporaryDirectory(TemporaryDirectory&&) = delete;
        TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

        //! Writes bytes to the file the loader would resolve this identifier to.
        void writeLibraryNamed(std::string_view identifier, const std::string& contents) const
        {
            std::ofstream out{ProgramLib::resolve(m_path, identifier), std::ios::binary};
            out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return m_path;
        }

    private:
        std::filesystem::path m_path;
    };

    /*!
        A second, independent reference to a library the loader is also using.

        Holding one keeps the module mapped after the loader unloads its own, which is the only way
        to ask a Program what happened to it after it was shut down. Both platforms reference-count
        by path, so this observes the same statics the loader's copy did rather than a fresh set.
    */
    class LifecycleObserver {
    public:
        explicit LifecycleObserver(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            m_handle = static_cast<void*>(LoadLibraryW(path.c_str()));
#else
            m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        }

        ~LifecycleObserver()
        {
            if (m_handle == nullptr) {
                return;
            }
#if defined(_WIN32)
            static_cast<void>(FreeLibrary(static_cast<HMODULE>(m_handle)));
#else
            static_cast<void>(dlclose(m_handle));
#endif
        }

        LifecycleObserver(const LifecycleObserver&) = delete;
        LifecycleObserver& operator=(const LifecycleObserver&) = delete;
        LifecycleObserver(LifecycleObserver&&) = delete;
        LifecycleObserver& operator=(LifecycleObserver&&) = delete;

        [[nodiscard]] bool opened() const noexcept
        {
            return m_handle != nullptr;
        }

        //! The fixture's record of which lifecycle entry points it has seen, or 0 if unreadable.
        [[nodiscard]] uint32_t flags() const noexcept
        {
            if (m_handle == nullptr) {
                return 0u;
            }

#if defined(_WIN32)
            const FARPROC symbol{GetProcAddress(static_cast<HMODULE>(m_handle), "tglTestProgramLifecycle")};
#else
            void* const symbol{dlsym(m_handle, "tglTestProgramLifecycle")};
#endif
            if (symbol == nullptr) {
                return 0u;
            }

            // Copied rather than cast, as in the loader: ISO C++ does not allow the conversion and
            // -Wpedantic is an error here.
            uint32_t (*read)(){nullptr};
            std::memcpy(&read, &symbol, sizeof(read));
            return read();
        }

    private:
        void* m_handle{nullptr};
    };

    constexpr uint32_t LIFECYCLE_INIT{1u};
    constexpr uint32_t LIFECYCLE_SHUTDOWN{2u};

    constexpr std::string_view GOOD_PROGRAM{"tgl_test_program"};

} // namespace

// ---------------------------------------------------------------------------------------------
// The identifier alphabet, which is the confinement rather than a tidiness rule
// ---------------------------------------------------------------------------------------------

TEST_CASE(an_ordinary_name_is_well_formed)
{
    // The success case first: a set of tests in which everything is rejected proves only that the
    // function is capable of rejecting.
    TEST_CHECK(ProgramLib::identifierIsWellFormed("quokka"));
    TEST_CHECK(ProgramLib::identifierIsWellFormed("tgl_test_program"));
    TEST_CHECK(ProgramLib::identifierIsWellFormed("worm-2"));
    TEST_CHECK(ProgramLib::identifierIsWellFormed("0"));
}

TEST_CASE(nothing_that_could_name_another_directory_is_well_formed)
{
    /*
        The whole reason the alphabet exists. Each of these is a real traversal somebody has used
        against something: not detected and refused, but unrepresentable, because the alphabet has no
        dot, no separator and no colon in it. There is deliberately no special case for "..".
    */
    TEST_CHECK(!ProgramLib::identifierIsWellFormed(".."));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("../../etc/passwd"));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("..\\..\\windows\\system32\\kernel32"));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("/usr/lib/libc"));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("C:\\windows\\system32\\kernel32"));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("\\\\server\\share\\payload"));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("sub/dir"));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("name.with.dots"));
}

TEST_CASE(an_empty_or_over_long_identifier_is_not_well_formed)
{
    TEST_CHECK(!ProgramLib::identifierIsWellFormed(""));
    TEST_CHECK(ProgramLib::identifierIsWellFormed(std::string(ProgramLib::MAX_IDENTIFIER_LENGTH, 'a')));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed(std::string(ProgramLib::MAX_IDENTIFIER_LENGTH + 1u, 'a')));
}

TEST_CASE(an_identifier_cannot_begin_with_punctuation)
{
    // A name beginning with a hyphen reads as an option wherever it is later printed or passed on.
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("-rf"));
    TEST_CHECK(!ProgramLib::identifierIsWellFormed("_hidden"));
}

TEST_CASE(an_embedded_null_does_not_truncate_an_identifier)
{
    /*
        A string_view carries its length, so an embedded null is an ordinary character here and the
        alphabet rejects it. It is worth a test because the C string this eventually becomes would
        stop there — an identifier that passed a check as one name and opened a file under another
        is the classic form of this bug.
    */
    TEST_CHECK(!ProgramLib::identifierIsWellFormed(std::string_view{"good\0../evil", 12u}));
}

TEST_CASE(resolution_applies_the_platform_decoration)
{
    const std::filesystem::path resolved{ProgramLib::resolve("/programs", "quokka")};

    TEST_CHECK_EQUAL(resolved.parent_path().generic_string(), std::string{"/programs"});
#if defined(_WIN32)
    TEST_CHECK_EQUAL(resolved.filename().string(), std::string{"quokka.dll"});
#else
    TEST_CHECK_EQUAL(resolved.filename().string(), std::string{"libquokka.so"});
#endif
}

// ---------------------------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------------------------

TEST_CASE(a_correct_program_loads_and_exposes_its_vtable)
{
    const ProgramLib::Library library{fixtureDirectory(), GOOD_PROGRAM, libraryInfo()};

    TEST_CHECK_EQUAL(library.identifier(), std::string{GOOD_PROGRAM});
    TEST_CHECK_EQUAL(library.vtable().abi_version, static_cast<uint32_t>(TGL_ABI_VERSION));
    TEST_CHECK(library.vtable().struct_size >= TGL_PROGRAM_VTABLE_MIN_SIZE);
    TEST_CHECK(library.vtable().program_tick != nullptr);
}

TEST_CASE(loading_calls_library_init_and_unloading_calls_library_shutdown)
{
    /*
        Both are no-ops in every fixture, so nothing else in this file could tell whether the loader
        called them at all. The observer holds its own reference to the same module, which keeps it
        mapped past the loader's unload and lets the answer be read afterwards.
    */
    const std::filesystem::path path{ProgramLib::resolve(fixtureDirectory(), GOOD_PROGRAM)};

    const LifecycleObserver observer{path};
    TEST_CHECK(observer.opened());

    uint32_t after_load{0u};
    {
        const ProgramLib::Library library{fixtureDirectory(), GOOD_PROGRAM, libraryInfo()};
        static_cast<void>(library.vtable());
        after_load = observer.flags();
    }

    // Read after the Library's destructor has run. The observer's own reference is what keeps the
    // module mapped long enough for there to be anything left to ask.
    const uint32_t after_unload{observer.flags()};

    TEST_CHECK((after_load & LIFECYCLE_INIT) != 0u);
    TEST_CHECK((after_load & LIFECYCLE_SHUTDOWN) == 0u);
    TEST_CHECK((after_unload & LIFECYCLE_SHUTDOWN) != 0u);
}

// ---------------------------------------------------------------------------------------------
// The ten refusals
// ---------------------------------------------------------------------------------------------

TEST_CASE(a_malformed_identifier_is_refused_before_the_filesystem_is_touched)
{
    TEST_CHECK(refusedBecause("../../etc/passwd", "not a well-formed identifier"));
    TEST_CHECK(refusedBecause("", "not a well-formed identifier"));
    TEST_CHECK(refusedBecause(std::string(ProgramLib::MAX_IDENTIFIER_LENGTH + 1u, 'a'), "not a well-formed identifier"));
}

TEST_CASE(a_missing_library_is_refused_by_name)
{
    TEST_CHECK(refusedBecause("tgl_no_such_program", "no library at"));
}

TEST_CASE(a_file_that_is_not_a_library_is_refused)
{
    /*
        Distinct from the missing case, and worth separating: this is the shape of a truncated
        download or a text file renamed, and it is the first point at which the operating system
        rather than the Grid decides the answer.
    */
    const TemporaryDirectory directory{"not_a_library"};
    directory.writeLibraryNamed("tgl_impostor", "This is not a shared library.\n");

    std::string message;
    try {
        const ProgramLib::Library library{directory.path(), "tgl_impostor", libraryInfo()};
        static_cast<void>(library.vtable());
    } catch (const std::runtime_error& error) {
        message = error.what();
    }

    TEST_CHECK(!message.empty());
    TEST_CHECK(message.find("refused to load") != std::string::npos);
}

TEST_CASE(a_library_exporting_no_entry_point_is_refused)
{
    TEST_CHECK(refusedBecause("tgl_broken_no_symbol", "exports no tglGetProgramVTable"));
}

TEST_CASE(a_program_that_declines_this_abi_version_is_refused)
{
    // Not a defect in the Program. It is the ABI working: it cannot satisfy this Grid and says so by
    // returning NULL, and the defect would be the Grid carrying on regardless.
    TEST_CHECK(refusedBecause("tgl_broken_refuses_version", "cannot satisfy ABI version"));
}

TEST_CASE(a_vtable_declaring_a_different_abi_version_is_refused)
{
    /*
        Reached only because the version is checked twice. This fixture ignores the version it was
        asked for and returns a vtable anyway, which is what a hand-written binding in another
        language does; without the second check it would load and then read the wrong bytes.
    */
    TEST_CHECK(refusedBecause("tgl_broken_wrong_version", "was built against ABI version"));
}

TEST_CASE(a_vtable_smaller_than_version_one_requires_is_refused)
{
    TEST_CHECK(refusedBecause("tgl_broken_small_vtable", "smaller than the"));
}

TEST_CASE(a_vtable_with_any_null_entry_point_is_refused)
{
    /*
        Nothing in C stops a Program leaving one out, and the symptom without this guard is a call
        through a null pointer on the first tick, inside code the Grid did not compile.

        All five rather than one, because the loader tests them in a single condition and a condition
        that named one of them twice would still pass a test that only nulled the third. Mutation
        found exactly that: with the first two clauses disabled the suite stayed green, because the
        one fixture that existed tripped a later clause.
    */
    TEST_CHECK(refusedBecause("tgl_broken_null_library_init", "left a required entry point null"));
    TEST_CHECK(refusedBecause("tgl_broken_null_program_rez", "left a required entry point null"));
    TEST_CHECK(refusedBecause("tgl_broken_null_program_tick", "left a required entry point null"));
    TEST_CHECK(refusedBecause("tgl_broken_null_program_derez", "left a required entry point null"));
    TEST_CHECK(refusedBecause("tgl_broken_null_library_shutdown", "left a required entry point null"));
}

TEST_CASE(a_refused_program_is_named_in_every_message)
{
    /*
        A run loads several Programs, so a refusal that does not say which one is a message that
        sends somebody looking through a roster. Checked across every refusal rather than one,
        because the identifier is prepended in a single place and it would be easy to add a path
        that bypasses it.
    */
    const std::vector<std::string_view> refused{"../../etc/passwd", "tgl_no_such_program", "tgl_broken_no_symbol", "tgl_broken_refuses_version",
        "tgl_broken_wrong_version", "tgl_broken_small_vtable", "tgl_broken_null_program_tick"};

    for (const std::string_view identifier : refused) {
        const std::string message{refusalFor(identifier)};
        TEST_CHECK(!message.empty());
        TEST_CHECK(message.find(std::string{identifier}) != std::string::npos);
    }
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
