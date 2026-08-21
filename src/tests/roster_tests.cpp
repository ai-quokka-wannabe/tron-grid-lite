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

#include "../roster.hpp"

#include <testing/testing.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

/*
    The first tests in this repository where a Program drives something. Everything here is the round
    trip: the Grid assembles senses, a library somebody else compiled decides what to do, and the
    Grid decides how much of that it is willing to act on.

    Still no Vulkan and still no device, which is the point of keeping the roster free of both. A
    creature moving in a straight line is a complete thing to check, and checking it needs no
    graphics driver at all.
*/

namespace
{

    [[nodiscard]] std::filesystem::path fixtureDirectory()
    {
        return std::filesystem::path{TGL_TEST_PROGRAM_DIR};
    }

    //! The source for ticks that test everything but the traced senses.
    [[nodiscard]] RosterLib::NullSensesSource& nullSenses()
    {
        static RosterLib::NullSensesSource source;
        return source;
    }

    [[nodiscard]] bool near(float a, float b, float tolerance = 1.0e-6f)
    {
        return std::fabs(a - b) <= tolerance;
    }

    //! The terrain every behaviour test stands on unless it says otherwise: flat, at height zero.
    [[nodiscard]] RosterLib::GroundFunction flatGround()
    {
        return [](float, float) {
            return 0.0f;
        };
    }

} // namespace
TEST_CASE(a_positive_yaw_turns_to_the_creatures_left)
{
    /*
        The ABI promises that a positive desired_turn_rate turns left seen from above, and this is
        the arithmetic that owes it. At rest a body faces -Z; a quarter turn to the left faces -X,
        because +X is the creature's right.
    */
    const MathLib::Vec3 at_rest{RosterLib::forwardFor(0.0f)};
    TEST_CHECK(near(at_rest.x, 0.0f));
    TEST_CHECK(near(at_rest.y, 0.0f));
    TEST_CHECK(near(at_rest.z, -1.0f));

    const MathLib::Vec3 quarter_left{RosterLib::forwardFor(1.5707964f)};
    TEST_CHECK(near(quarter_left.x, -1.0f));
    TEST_CHECK(near(quarter_left.y, 0.0f));
    TEST_CHECK(near(quarter_left.z, 0.0f));
}

TEST_CASE(a_body_point_is_carried_by_the_pose)
{
    // At rest the body frame is the world frame shifted: a sensor's position is an offset.
    const RosterLib::Pose at_rest{.position = MathLib::Vec3{5.0f, 1.0f, -3.0f}, .yaw = 0.0f};
    const MathLib::Vec3 carried{RosterLib::worldFromBody(at_rest, MathLib::Vec3{0.0f, 0.5f, -0.2f})};
    TEST_CHECK(near(carried.x, 5.0f));
    TEST_CHECK(near(carried.y, 1.5f));
    TEST_CHECK(near(carried.z, -3.2f));

    /*
        A quarter turn left must carry a forward-mounted sensor to where the body now faces —
        the same promise forwardFor makes, checked through the general transform: a point 0.2 m
        ahead on the body ends up 0.2 m along -X in the world.
    */
    const RosterLib::Pose turned{.position = MathLib::Vec3{}, .yaw = 1.5707964f};
    const MathLib::Vec3 ahead{RosterLib::worldFromBody(turned, MathLib::Vec3{0.0f, 0.0f, -0.2f})};
    TEST_CHECK(near(ahead.x, -0.2f));
    TEST_CHECK(near(ahead.y, 0.0f));
    TEST_CHECK(near(ahead.z, 0.0f));
}

TEST_CASE(the_senses_source_is_asked_once_per_creature_per_tick)
{
    /*
        The seam itself: the roster must consult the source after the kinematic senses are set and
        once for every creature it ticks, because a source that is skipped produces a creature that
        is deaf without anything failing.
    */
    class CountingSource final : public RosterLib::SensesSource {
    public:
        void fill(const RosterLib::Creature& creature, TglSenses& senses) override
        {
            ++calls;
            saw_dt = (senses.dt_seconds == RosterLib::TICK_SECONDS);
            saw_ears_declared = (creature.body.ear_count == 2u) && (creature.body.ears != nullptr);
            saw_eyes_declared = (creature.body.eye_count == 2u) && (creature.body.eyes != nullptr) && (creature.body.irradiance_sample_count == 64u);
        }

        uint32_t calls{0u};
        bool saw_dt{false};
        bool saw_ears_declared{false};
        bool saw_eyes_declared{false};
    };

    CountingSource source;
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 3u, flatGround()};
    roster.tick(source);
    roster.tick(source);

    TEST_CHECK_EQUAL(source.calls, 6u);
    TEST_CHECK(source.saw_dt);

    // The first body declares two ears and two eyes, whose descriptors must survive past the rez
    // call — the roster's copy carries pointers, and this is where they are proven still valid.
    TEST_CHECK(source.saw_ears_declared);
    TEST_CHECK(source.saw_eyes_declared);
}
TEST_CASE(a_program_that_declines_to_rez_stops_the_roster)
{
    // A null handle is a refusal rather than a creature, and carrying on with one would mean calling
    // program_tick with a pointer the Program never issued.
    std::string message;
    try {
        RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_refuses_rez", 1u, flatGround()};
        roster.tick(nullSenses());
    } catch (const std::runtime_error& error) {
        message = error.what();
    }

    TEST_CHECK(!message.empty());
    TEST_CHECK(message.find("refused to rez") != std::string::npos);
}

TEST_CASE(every_creature_in_a_roster_gets_its_own_turn_and_its_own_seed)
{
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 3u, flatGround()};
    roster.tick(nullSenses());
    roster.tick(nullSenses());

    const std::vector<RosterLib::Creature>& creatures{roster.creatures()};
    TEST_CHECK_EQUAL(creatures.size(), static_cast<size_t>(3u));

    for (const RosterLib::Creature& creature : creatures) {
        // The mind's half is what lives here now: each Program was called on its own body and
        // its ask was staged raw - applying it is the world's physics, which is Master Control's.
        TEST_CHECK(near(creature.staged.desired_forward_speed, 0.5f));
    }

    // Derived from the roster index, so that two creatures of one roster never share a seed and the
    // same roster produces the same seeds on every run.
    TEST_CHECK(creatures[0].body.random_seed != creatures[1].body.random_seed);
    TEST_CHECK(creatures[1].body.random_seed != creatures[2].body.random_seed);
}

TEST_CASE(the_tick_length_is_exact_and_is_what_a_program_is_told)
{
    // The number spans a C ABI: TglLibraryInfo::nominal_dt_seconds, TglSenses::dt_seconds, and the
    // constant the Grid integrates against. This is the Grid-side end of that.
    TEST_CHECK_EQUAL(RosterLib::TICK_SECONDS, 0.03125f);
    TEST_CHECK_EQUAL(RosterLib::TICK_SECONDS * static_cast<float>(RosterLib::TICKS_PER_SECOND), 1.0f);

    // 145 hours of ticks, still exact. The property 25 Hz and 50 Hz do not have.
    const float far_out{static_cast<float>(16u * 1024u * 1024u) * RosterLib::TICK_SECONDS};
    TEST_CHECK_EQUAL(far_out, 524288.0f);
}
TEST_CASE(a_model_offered_at_rez_arrives_whole)
{
    /*
        The one field in the interface the Program authors rather than receives: the modelled
        fixture offers a four-vertex pyramid with a glowing tail, and what the Grid keeps must be
        exactly what was offered — counts, indices and values alike — because the ABI's arrays die
        with the rez call and this copy is the shape's whole survival.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_modelled", 1u, flatGround()};
    const RosterLib::CreatureModel& model{roster.creatures().front().model};

    TEST_CHECK(!model.empty());
    TEST_CHECK_EQUAL(model.vertex_positions.size(), 4u);
    TEST_CHECK_EQUAL(model.triangles.size(), 4u);
    TEST_CHECK_EQUAL(model.materials.size(), 2u);

    // Facts strong enough to catch a shuffled or truncated copy rather than merely a missing one.
    TEST_CHECK_EQUAL(model.vertex_positions.front().z, -0.2f); // The nose points forward.
    TEST_CHECK_EQUAL(model.triangles.back().material, 1u); // The tail wears the second material.
    TEST_CHECK(model.materials[1].emission[2] > 0.0f); // And the tail glows.
    TEST_CHECK_EQUAL(model.materials[0].transmission, 0.0f); // The hull is an opaque mirror.
}

TEST_CASE(a_model_naming_a_vertex_that_does_not_exist_refuses_the_whole_rez)
{
    // The misshapen fixture's port flank names vertex nine of a model with four. Accepting the
    // salvageable part would ship a body its author never saw; the Grid refuses the rez outright.
    std::string message;
    try {
        RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_misshapen", 1u, flatGround()};
        roster.tick(nullSenses());
    } catch (const std::runtime_error& error) {
        message = error.what();
    }

    TEST_CHECK(!message.empty());
    TEST_CHECK(message.find("a model the Grid refuses") != std::string::npos);
    TEST_CHECK(message.find("names vertex 9") != std::string::npos);
}

TEST_CASE(a_program_that_offers_no_model_stays_bodiless)
{
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, flatGround()};
    TEST_CHECK(roster.creatures().front().model.empty());
}

TEST_CASE(a_call_sounds_on_the_tick_after_it_is_staged)
{
    /*
        The voice is an actuator like the wheels: what a Program returns is staged, and physics
        acts on it next tick for every creature alike. The fixture calls from its very first tick,
        so the first tick's voice is the zeroed default and the second tick's is the call — the
        staging delay, observed in the one actuator with no traction condition.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_calling", 1u, flatGround()};

    roster.tick(nullSenses());
    TEST_CHECK_EQUAL(roster.creatures().front().vocalisation, 0.0f);

    roster.tick(nullSenses());
    TEST_CHECK_EQUAL(roster.creatures().front().vocalisation, 0.75f);
}
int main()
{
    return static_cast<int>(TestingLib::runAll());
}
