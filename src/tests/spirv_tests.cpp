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

#include "../spirv.hpp"
#include <testing/testing.hpp>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

/*
    Every test here exercises a failure. That is deliberate and it is the reason the file exists.

    The audit that produced these found several defects in this repository, and every one of them was
    in code that only runs after something has already gone wrong: an abort where a refusal belonged,
    a destructor that could terminate the process, a thread whose entry point had no catch. None was
    reachable by any check the repository owned, because the reference render walks the success path
    and nothing else does anything at all. An error path that has never run is indistinguishable from
    one that works.

    These need no GPU and no Vulkan SDK, which is why `spirv.hpp` mentions neither. They run under
    ctest on every push, including on a machine that has never seen a graphics driver.

    **Five of the six guards were confirmed by mutation**: deleting the open check, the size check,
    either half of the size check, or the magic-number check turns this file red. The sixth — the
    short-read check — is not covered, and saying so is more useful than implying it is. Reaching it
    means making a file shrink between `tellg` reporting its size and `read` consuming it, which is a
    filesystem race rather than something a test can arrange portably. Its reasoning is written where
    it lives: a short read leaves a zero-filled tail that the driver's SPIR-V parser consumes as
    instructions.
*/

namespace
{

    //! A file that deletes itself, so a failing assertion cannot leave litter behind.
    class TemporaryFile {
    public:
        explicit TemporaryFile(const std::string& name) :
            m_path(std::filesystem::temp_directory_path() / ("tgl_spirv_test_" + name))
        {
            std::filesystem::remove(m_path);
        }

        ~TemporaryFile()
        {
            std::error_code ignored;
            std::filesystem::remove(m_path, ignored);
        }

        TemporaryFile(const TemporaryFile&) = delete;
        TemporaryFile& operator=(const TemporaryFile&) = delete;
        TemporaryFile(TemporaryFile&&) = delete;
        TemporaryFile& operator=(TemporaryFile&&) = delete;

        //! Writes raw bytes, which is how a file of a deliberately wrong size gets made.
        void writeBytes(const std::vector<unsigned char>& bytes) const
        {
            std::ofstream out{m_path, std::ios::binary};
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        //! Writes whole words, which is how a valid module gets made.
        void writeWords(const std::vector<uint32_t>& words) const
        {
            std::ofstream out{m_path, std::ios::binary};
            out.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
        }

        [[nodiscard]] std::string path() const
        {
            return m_path.string();
        }

    private:
        std::filesystem::path m_path;
    };

    /*!
        Returns the refusal message, or an empty string when the read succeeded.

        The message rather than a bare true, because "it threw" is too weak an assertion to pin
        anything. Mutation testing proved it: deleting the `is_open` check entirely left every test
        in this file green, because a file that does not exist makes `tellg` return −1 and the *size*
        check fires instead. The test claimed to cover the open guard and covered its neighbour.
    */
    [[nodiscard]] std::string refusalFor(const std::string& path)
    {
        try {
            const std::vector<uint32_t> words{SpirvLib::read(path)};
            static_cast<void>(words);
            return {};
        } catch (const std::runtime_error& error) {
            return error.what();
        }
    }

    //! Returns true when the refusal message names the expected guard.
    [[nodiscard]] bool refusedBecause(const std::string& path, const std::string& expected)
    {
        const std::string message{refusalFor(path)};
        return (!message.empty()) && (message.find(expected) != std::string::npos);
    }

} // namespace

TEST_CASE(a_valid_module_reads_back_word_for_word)
{
    // The success case first, because a set of tests in which everything fails proves only that the
    // function is capable of failing.
    const TemporaryFile file{"valid.spv"};
    const std::vector<uint32_t> written{SpirvLib::SPIRV_MAGIC, 0x00010600u, 0u, 42u};
    file.writeWords(written);

    const std::vector<uint32_t> read{SpirvLib::read(file.path())};

    TEST_CHECK_EQUAL(read.size(), written.size());
    for (size_t index{0u}; index < written.size(); ++index) {
        TEST_CHECK_EQUAL(read[index], written[index]);
    }
}

TEST_CASE(a_missing_file_is_refused)
{
    const TemporaryFile file{"absent.spv"}; // Constructed and never written, so it does not exist.
    TEST_CHECK(refusedBecause(file.path(), "Failed to open"));
}

TEST_CASE(an_empty_file_is_refused)
{
    // Zero bytes is a whole number of words, so the size test has to reject it explicitly rather
    // than relying on the modulo. It matters because the magic-number check reads words.front(),
    // and on an empty vector that is a read past the end rather than a diagnosis.
    const TemporaryFile file{"empty.spv"};
    file.writeBytes({});

    TEST_CHECK(refusedBecause(file.path(), "invalid size"));
}

TEST_CASE(a_file_that_is_not_a_whole_number_of_words_is_refused)
{
    const TemporaryFile file{"ragged.spv"};
    file.writeBytes({0x03u, 0x02u, 0x23u, 0x07u, 0x00u}); // Correct magic, then one byte too many.

    TEST_CHECK(refusedBecause(file.path(), "invalid size"));
}

TEST_CASE(a_file_that_is_not_spirv_is_refused)
{
    /*
        The likeliest mistake in practice: a path that resolves to a real, readable file of the right
        shape that simply is not a shader. Without the magic-number check this reaches the driver's
        SPIR-V parser, which is a far worse place to find out.
    */
    const TemporaryFile file{"impostor.spv"};
    file.writeWords({0xDEADBEEFu, 0x00010600u, 0u, 0u});

    TEST_CHECK(refusedBecause(file.path(), "Not a SPIR-V module"));
}

TEST_CASE(a_directory_is_refused_rather_than_read)
{
    // A path that exists and is readable in every sense except the one that matters. Which guard
    // fires differs by platform — an open that fails outright, or one that succeeds and then
    // reports a size no file has — so this is the one case that asserts the refusal rather than
    // the reason.
    const std::string directory{std::filesystem::temp_directory_path().string()};

    TEST_CHECK(!refusalFor(directory).empty());
}

TEST_CASE(the_smallest_legal_module_is_one_word)
{
    /*
        A single word is a whole number of words, is non-empty, and carries the magic — so it must be
        accepted. This is the boundary the size check sits on, and a `< 4` written where `<= 0`
        belongs would still pass every other test in this file.
    */
    const TemporaryFile file{"one_word.spv"};
    file.writeWords({SpirvLib::SPIRV_MAGIC});

    const std::vector<uint32_t> read{SpirvLib::read(file.path())};
    TEST_CHECK_EQUAL(read.size(), static_cast<size_t>(1));
    TEST_CHECK_EQUAL(read.front(), SpirvLib::SPIRV_MAGIC);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
