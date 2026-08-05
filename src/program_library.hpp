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

#pragma once

#include <tgl/tgl_program_abi.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

/*!
    Bringing a Program library into the process, and refusing everything that is not one.

    **Nothing here mentions Vulkan**, for the reason `spirv.hpp` gives: every interesting path in this
    file is a refusal, refusals are the code that only runs once something has already gone wrong, and
    an error path that cannot be tested without a GPU is an error path nobody tests. All ten of them
    run under `ctest` on a machine that has never had a graphics driver.

    A Program is named, never pathed. The Grid takes an identifier and resolves it against a directory
    it already trusts, so no separator, no `..` and no drive letter can arrive from outside — see
    `identifierIsWellFormed`. That is a requirement rather than caution: the roster resolves Programs
    out of a configuration file, and a configuration file is something a downloaded creature pack can
    write.
*/
namespace ProgramLib
{

    /*!
        Longest identifier the Grid will consider.

        A bound rather than a judgement about names: every path the resolved identifier lands in has
        a limit somewhere, and a refusal naming the identifier is a better outcome than a truncation
        that silently names a different file.
    */
    inline constexpr size_t MAX_IDENTIFIER_LENGTH{64u};

    /*!
        Whether an identifier is one the Grid will resolve at all.

        The rule is an allowlist — ASCII letters, digits, underscore and hyphen, beginning with a
        letter or digit — and it is deliberately narrower than what a filesystem would accept. The
        capability is removed rather than guarded, which is the shape that worked when the same
        problem came up in `tools/record_flyby.py`: a check that a path is *probably* safe is a check
        somebody has to keep being right about, whereas an alphabet containing no separator, no dot
        and no colon cannot express a traversal in the first place.

        Rejecting the dot is what makes `..` unrepresentable rather than merely detected, and it is
        why this function has no special case for it.

        \param identifier Candidate name, from a configuration file or the command line.
        \return True when the identifier is well formed. Never throws, so it is usable in a check.
    */
    [[nodiscard]] bool identifierIsWellFormed(std::string_view identifier) noexcept;

    /*!
        The directory Program identifiers resolve against: `programs/` beside the executable.

        Beside the *executable*, deliberately, and never the working directory. The whole point of
        resolving a name against a trusted directory is that the directory is a property of the
        installation rather than of the invocation — if it followed the working directory, anyone who
        could choose where the Grid was launched from could choose which binary a roster entry named,
        and the confinement would only be moving the question rather than answering it.

        \return `<directory containing this executable>/programs`.
        \throws std::runtime_error if the operating system will not say where the executable is.
    */
    [[nodiscard]] std::filesystem::path defaultDirectory();

    /*!
        The file an identifier names inside a directory, with the platform's own decoration applied:
        `<identifier>.dll` on Windows, `lib<identifier>.so` on Linux, matching what CMake builds.

        \param directory Directory the Grid trusts. It supplies this; it never comes from a Program.
        \param identifier Name to resolve.
        \return The path the loader would open. It is not checked for existence here.
        \throws std::runtime_error when the identifier is not well formed.
    */
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& directory, std::string_view identifier);

    //! What a Program library says about itself, once it has passed every check.
    struct Inspection {
        uint32_t abi_version{0u};
        uint32_t struct_size{0u};
    };

    /*!
        Answers whether a library is a Program the Grid could run, and unloads it again.

        Every check `Library` makes, and none of the consequences: the library is loaded, its symbol
        resolved and its vtable validated, then it is dropped. `library_init` is deliberately not
        called, which is what makes this answerable at all right now — `TglLibraryInfo` carries the
        tick length, the Grid's tick rate is not yet chosen, and a check that invented one would be
        handing a Program a number the run would later contradict.

        Only the two version numbers come back. Everything else in a vtable is a pointer into a
        module that is unmapped before this returns.

        \param directory Directory the Grid trusts to hold Program libraries.
        \param identifier Name of the Program, not a path.
        \return What the library declared about itself.
        \throws std::runtime_error naming the identifier and the reason, exactly as `Library` would.
    */
    [[nodiscard]] Inspection inspect(const std::filesystem::path& directory, std::string_view identifier);

    /*!
        One loaded Program library, and the vtable it exported.

        Owns the operating system's handle: the constructor loads and validates, and the destructor
        calls `library_shutdown` and unloads. A constructed object is therefore a library that passed
        every check, which is what lets the rest of the Grid read `vtable()` without asking questions.

        Individual creatures are not rezzed here. This is the library scope — `dlopen` and `dlclose`,
        facts about the operating system rather than events on the Grid, which is why the ABI gives
        those two entry points plain names.
    */
    class Library {
    public:
        /*!
            Loads the named library, validates what it exports, and calls `library_init`.

            Ten ways this refuses, and every one of them is something that happens: the identifier is
            empty, too long, or contains a character the alphabet does not allow; the file is not
            there; the operating system will not load it; it exports no `tglGetProgramVTable`; that
            function returns NULL because the Program cannot satisfy this ABI version; the vtable
            disagrees about the version anyway; it claims a size smaller than version 1 requires; or
            one of the five required entry points is null.

            \param directory Directory the Grid trusts to hold Program libraries.
            \param identifier Name of the Program, not a path.
            \param info Facts handed to `library_init`, which is called once the library is accepted.
            \throws std::runtime_error naming the identifier and the reason, for any of the ten.
        */
        Library(const std::filesystem::path& directory, std::string_view identifier, const TglLibraryInfo& info);

        ~Library();

        Library(const Library&) = delete;
        Library& operator=(const Library&) = delete;
        Library(Library&&) = delete;
        Library& operator=(Library&&) = delete;

        //! The exported vtable. Valid for this object's lifetime, and never null.
        [[nodiscard]] const TglProgramVTable& vtable() const noexcept;

        //! The identifier this was loaded from, for diagnostics.
        [[nodiscard]] const std::string& identifier() const noexcept;

    private:
        std::string m_identifier;

        //! The operating system's handle, kept as void* so that <windows.h> stays out of this header.
        void* m_handle{nullptr};

        const TglProgramVTable* m_vtable{nullptr};
    };

} // namespace ProgramLib
