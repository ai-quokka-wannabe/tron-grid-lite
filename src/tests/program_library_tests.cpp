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

TEST_CASE(the_default_directory_sits_beside_the_executable_and_not_beside_the_caller)
{
    /*
        The property that matters is not the name `programs` but that the directory follows the
        executable rather than the working directory. If it followed the caller, whoever chose where
        the Grid was launched from would choose which binary a roster entry named, and the
        confinement would have moved the question rather than answered it.

        Checked by running from somewhere else and getting the same answer.
    */
    const std::filesystem::path from_here{ProgramLib::defaultDirectory()};

    const std::filesystem::path original{std::filesystem::current_path()};
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    const std::filesystem::path from_elsewhere{ProgramLib::defaultDirectory()};
    std::filesystem::current_path(original);

    TEST_CHECK_EQUAL(from_here.generic_string(), from_elsewhere.generic_string());
    TEST_CHECK_EQUAL(from_here.filename().string(), std::string{"programs"});
    TEST_CHECK(from_here.is_absolute());

    // Its parent is the directory this test executable is sitting in, which is the one thing about
    // the answer this test can check independently.
    TEST_CHECK(std::filesystem::exists(from_here.parent_path()));
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

TEST_CASE(inspecting_reports_what_the_library_declares)
{
    const ProgramLib::Inspection inspection{ProgramLib::inspect(fixtureDirectory(), GOOD_PROGRAM)};

    TEST_CHECK_EQUAL(inspection.abi_version, static_cast<uint32_t>(TGL_ABI_VERSION));
    TEST_CHECK(inspection.struct_size >= TGL_PROGRAM_VTABLE_MIN_SIZE);
}

TEST_CASE(inspecting_does_not_call_library_init)
{
    /*
        The property the check is built around, and the reason it exists as something separate from
        loading. TglLibraryInfo carries the tick length; the Grid's tick rate is not chosen yet; and
        a check that invented one would hand a Program a number the eventual run would contradict.
        So it answers "could this run?" without starting anything.

        Observable only because the fixture records what it was asked to do.
    */
    const std::filesystem::path path{ProgramLib::resolve(fixtureDirectory(), GOOD_PROGRAM)};

    const LifecycleObserver observer{path};
    TEST_CHECK(observer.opened());
    TEST_CHECK_EQUAL(observer.flags(), 0u);

    static_cast<void>(ProgramLib::inspect(fixtureDirectory(), GOOD_PROGRAM));

    TEST_CHECK_EQUAL(observer.flags(), 0u);
}

TEST_CASE(inspecting_refuses_exactly_what_loading_refuses)
{
    /*
        The two share their validation, and this is what holds them together. A check that passed a
        Program the run would then reject would be worse than having no check: it would move the
        failure to the least convenient moment and attach the Grid's blessing to it on the way.
    */
    const std::vector<std::string_view> refused{"../../etc/passwd", "tgl_no_such_program", "tgl_broken_no_symbol", "tgl_broken_refuses_version",
        "tgl_broken_wrong_version", "tgl_broken_small_vtable", "tgl_broken_null_program_tick"};

    for (const std::string_view identifier : refused) {
        std::string inspection_message;
        try {
            static_cast<void>(ProgramLib::inspect(fixtureDirectory(), identifier));
        } catch (const std::runtime_error& error) {
            inspection_message = error.what();
        }

        TEST_CHECK_EQUAL(inspection_message, refusalFor(identifier));
        TEST_CHECK(!inspection_message.empty());
    }
}

TEST_CASE(listing_finds_every_program_and_says_which_ones_load)
{
    const std::vector<ProgramLib::Listing> listings{ProgramLib::list(fixtureDirectory())};

    const auto find = [&listings](std::string_view identifier) -> const ProgramLib::Listing* {
        for (const ProgramLib::Listing& listing : listings) {
            if (listing.identifier == identifier) {
                return &listing;
            }
        }
        return nullptr;
    };

    const ProgramLib::Listing* const good{find(GOOD_PROGRAM)};
    TEST_CHECK(good != nullptr);
    if (good != nullptr) {
        TEST_CHECK(good->refusal.empty());
        TEST_CHECK_EQUAL(good->inspection.abi_version, static_cast<uint32_t>(TGL_ABI_VERSION));
    }

    // A broken one is present and reported as broken rather than quietly dropped. A listing that
    // showed only what works would hide the one file its reader is looking for.
    const ProgramLib::Listing* const broken{find("tgl_broken_small_vtable")};
    TEST_CHECK(broken != nullptr);
    if (broken != nullptr) {
        TEST_CHECK(!broken->refusal.empty());
        TEST_CHECK(broken->refusal.find("smaller than the") != std::string::npos);
    }

    // Ordered, so that two runs over one folder agree and a reader can compare them.
    for (size_t index{1u}; index < listings.size(); ++index) {
        TEST_CHECK(listings[index - 1u].identifier < listings[index].identifier);
    }
}

TEST_CASE(listing_an_empty_directory_finds_nothing_rather_than_failing)
{
    const TemporaryDirectory directory{"empty_listing"};
    TEST_CHECK(ProgramLib::list(directory.path()).empty());
}

TEST_CASE(a_missing_program_directory_says_so_rather_than_naming_a_file)
{
    /*
        The first thing a new User meets, because nothing creates `programs/`. Without this the
        refusal names a file inside a folder that does not exist, which sends them looking for the
        wrong thing entirely.
    */
    const std::filesystem::path absent{std::filesystem::temp_directory_path() / "tgl_program_test_absent_directory"};
    std::error_code ignored;
    std::filesystem::remove_all(absent, ignored);

    std::string message;
    try {
        static_cast<void>(ProgramLib::inspect(absent, GOOD_PROGRAM));
    } catch (const std::runtime_error& error) {
        message = error.what();
    }

    TEST_CHECK(!message.empty());
    TEST_CHECK(message.find("no Program directory at") != std::string::npos);
    TEST_CHECK(message.find("no library at") == std::string::npos);
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

TEST_CASE(a_library_under_the_other_toolchains_name_is_refused_and_the_difference_pointed_out)
{
    /*
        MinGW prefixes shared libraries with `lib` on Windows as well as on Linux, so a Program built
        with it lands as `libquokka.dll` where the Grid resolves `quokka.dll`. The Grid is right to
        refuse — one identifier resolves to one filename, and two candidate spellings would mean two
        possible binaries for one name — but "no library at ..." beside a directory that visibly
        contains the thing is four characters somebody has to spot.

        This is not hypothetical. It is how this loader first failed CI, on the one preset that
        cannot be built on the development machine.
    */
    const TemporaryDirectory directory{"other_toolchain"};

    const std::filesystem::path expected{ProgramLib::resolve(directory.path(), "tgl_misnamed")};
    const std::string suffix{expected.extension().string()};
    const bool prefixed{expected.filename().string().rfind("lib", 0u) == 0u};
    const std::string other_name{prefixed ? "tgl_misnamed" + suffix : "libtgl_misnamed" + suffix};

    std::ofstream out{directory.path() / other_name, std::ios::binary};
    out << "not a real library, but it is named the way the other toolchain names one";
    out.close();

    std::string message;
    try {
        const ProgramLib::Library library{directory.path(), "tgl_misnamed", libraryInfo()};
        static_cast<void>(library.vtable());
    } catch (const std::runtime_error& error) {
        message = error.what();
    }

    TEST_CHECK(!message.empty());
    TEST_CHECK(message.find("no library at") != std::string::npos);
    TEST_CHECK(message.find(other_name) != std::string::npos);
    TEST_CHECK(message.find("named exactly") != std::string::npos);
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

TEST_CASE(the_grid_hosts_one_program_library_per_run)
{
    /*
        One live library per process, enforced rather than assumed. The replay claim names one tick
        loop driving one roster; TglLibraryInfo::creature_count is promised exact for the run; and
        a second library would put two Programs' process-wide state — locales, signal handlers,
        whatever a GUI toolkit drags in — into one address space with no rule about who wins.
    */
    const ProgramLib::Library first{fixtureDirectory(), GOOD_PROGRAM, libraryInfo()};

    std::string message;
    try {
        const ProgramLib::Library second{fixtureDirectory(), GOOD_PROGRAM, libraryInfo()};
    } catch (const std::runtime_error& error) {
        message = error.what();
    }

    TEST_CHECK(!message.empty());
    TEST_CHECK(message.find("one Program library per run") != std::string::npos);
}

TEST_CASE(a_shut_down_library_frees_the_slot_for_the_next)
{
    // Sequential loads are legitimate and must stay so: the listing probes its candidates one at a
    // time, each unloaded before the next is opened.
    {
        const ProgramLib::Library first{fixtureDirectory(), GOOD_PROGRAM, libraryInfo()};
        TEST_CHECK(!first.identifier().empty());
    }

    const ProgramLib::Library second{fixtureDirectory(), GOOD_PROGRAM, libraryInfo()};
    TEST_CHECK(!second.identifier().empty());
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
