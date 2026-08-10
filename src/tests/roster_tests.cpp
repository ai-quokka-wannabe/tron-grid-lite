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

    //! Bounds of the body every creature currently gets, repeated here so a drifting bound is caught
    //! rather than absorbed. The roster owns the real ones; this is the other side of that fact.
    constexpr float MAX_FORWARD_SPEED{1.0f};
    constexpr float MAX_TURN_RATE{1.5707964f};

    [[nodiscard]] bool near(float a, float b, float tolerance = 1.0e-6f)
    {
        return std::fabs(a - b) <= tolerance;
    }

    [[nodiscard]] TglCreatureDesc boundedBody()
    {
        TglCreatureDesc desc{};
        desc.max_forward_speed = MAX_FORWARD_SPEED;
        desc.max_turn_rate = MAX_TURN_RATE;
        desc.max_vertical_speed = 0.0f;
        desc.max_vocalisation_strength = 1.0f;
        return desc;
    }

} // namespace

// ---------------------------------------------------------------------------------------------
// A NaN request must become zero, and never a bound
// ---------------------------------------------------------------------------------------------

TEST_CASE(an_ordinary_request_passes_through_untouched)
{
    // The success case first: a function that only ever rejects proves nothing about what it allows.
    TglActions actions{};
    actions.desired_forward_speed = 0.5f;
    actions.desired_turn_rate = -0.25f;
    actions.vocalisation_strength = 0.75f;

    RosterLib::sanitiseAndClamp(actions, boundedBody());

    TEST_CHECK(near(actions.desired_forward_speed, 0.5f));
    TEST_CHECK(near(actions.desired_turn_rate, -0.25f));
    TEST_CHECK(near(actions.vocalisation_strength, 0.75f));
}

TEST_CASE(a_request_beyond_the_body_is_clamped_in_both_directions)
{
    TglActions actions{};
    actions.desired_forward_speed = 10.0f;
    actions.desired_turn_rate = -100.0f;

    RosterLib::sanitiseAndClamp(actions, boundedBody());

    TEST_CHECK(near(actions.desired_forward_speed, MAX_FORWARD_SPEED));
    TEST_CHECK(near(actions.desired_turn_rate, -MAX_TURN_RATE));
}

TEST_CASE(a_bound_of_zero_removes_the_actuator_entirely)
{
    // How a body without an actuator is expressed: not a missing field, but a bound of zero. A
    // Program asking to climb is told no by the same mechanism that tells it how fast it may walk.
    TglActions actions{};
    actions.desired_vertical_speed = 5.0f;

    RosterLib::sanitiseAndClamp(actions, boundedBody());

    TEST_CHECK(near(actions.desired_vertical_speed, 0.0f));
}

TEST_CASE(a_non_finite_request_becomes_zero_rather_than_propagating)
{
    /*
        The one that matters. A NaN velocity becomes a NaN position and then a hierarchy traversal
        that never terminates, so a Program returning garbage takes the Grid down rather than merely
        behaving oddly.

        Zero rather than a bound, and that is the sharper half. A clamp built on fmin/fmax returns
        the non-NaN operand, so it would answer a NaN request with `max_forward_speed` — the creature
        would sprint, nothing would look wrong, and the Program would never learn it had asked for
        nonsense. This asserts the outcome rather than the ordering that produces it, because
        mutation showed the ordering is not observable with a comparison-based clamp: every
        comparison against NaN is false, so the NaN falls through to be caught either way.
    */
    TglActions actions{};
    actions.desired_forward_speed = std::numeric_limits<float>::quiet_NaN();
    actions.desired_turn_rate = std::numeric_limits<float>::infinity();
    actions.desired_vertical_speed = -std::numeric_limits<float>::infinity();
    actions.vocalisation_strength = std::numeric_limits<float>::quiet_NaN();

    RosterLib::sanitiseAndClamp(actions, boundedBody());

    TEST_CHECK(std::isfinite(actions.desired_forward_speed));
    TEST_CHECK(std::isfinite(actions.desired_turn_rate));
    TEST_CHECK(std::isfinite(actions.desired_vertical_speed));
    TEST_CHECK(std::isfinite(actions.vocalisation_strength));
    TEST_CHECK(near(actions.desired_forward_speed, 0.0f));
    TEST_CHECK(near(actions.vocalisation_strength, 0.0f));
}

TEST_CASE(a_negative_loudness_is_silence_rather_than_a_reflected_magnitude)
{
    // A call is loudness, not a signed quantity, so the negative half of the range is meaningless
    // rather than symmetrical. Clamping it by magnitude like a velocity would make a Program asking
    // for -5 shout at full volume.
    TglActions actions{};
    actions.vocalisation_strength = -5.0f;

    RosterLib::sanitiseAndClamp(actions, boundedBody());

    TEST_CHECK(near(actions.vocalisation_strength, 0.0f));
}

// ---------------------------------------------------------------------------------------------
// The body frame
// ---------------------------------------------------------------------------------------------

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
        }

        uint32_t calls{0u};
        bool saw_dt{false};
        bool saw_ears_declared{false};
    };

    CountingSource source;
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 3u};
    roster.tick(source);
    roster.tick(source);

    TEST_CHECK_EQUAL(source.calls, 6u);
    TEST_CHECK(source.saw_dt);

    // The first body now declares two ears, whose descriptors must survive past the rez call —
    // the roster's copy carries pointers, and this is where they are proven still valid.
    TEST_CHECK(source.saw_ears_declared);
}

// ---------------------------------------------------------------------------------------------
// A Program driving a body
// ---------------------------------------------------------------------------------------------

TEST_CASE(a_steady_program_moves_its_body_exactly_where_it_asked)
{
    /*
        Exact rather than approximate, and the tick rate is why. Half a metre a second at 0.03125 s a
        tick is 0.015625 m a tick, and both are representable in binary32, so thirty-two of them is
        half a metre to the last bit. At 25 or 50 Hz this assertion would need a tolerance, and the
        tolerance would be hiding the drift rather than measuring it.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u};

    for (uint32_t index{0u}; index < RosterLib::TICKS_PER_SECOND; ++index) {
        roster.tick(nullSenses());
    }

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK_EQUAL(roster.tickCount(), static_cast<uint64_t>(RosterLib::TICKS_PER_SECOND));
    TEST_CHECK_EQUAL(creature.pose.position.z, -0.5f);
    TEST_CHECK_EQUAL(creature.pose.position.x, 0.0f);
    TEST_CHECK_EQUAL(creature.pose.position.y, 0.0f);
    TEST_CHECK_EQUAL(creature.pose.yaw, 0.0f);
}

TEST_CASE(a_program_asking_for_more_than_its_body_has_gets_the_body)
{
    /*
        The same loop with a Program that asks for ten metres a second, a hundred radians a second, a
        NaN vertical speed and a negative shout. What arrives at the pose is the body's own limits,
        and nothing that is not a number reaches it.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_excessive", 1u};
    roster.tick(nullSenses());

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK(near(creature.forward_speed, MAX_FORWARD_SPEED));
    TEST_CHECK(near(creature.turn_rate, MAX_TURN_RATE));
    TEST_CHECK(near(creature.vertical_speed, 0.0f));

    TEST_CHECK(std::isfinite(creature.pose.position.x));
    TEST_CHECK(std::isfinite(creature.pose.position.y));
    TEST_CHECK(std::isfinite(creature.pose.position.z));
    TEST_CHECK(std::isfinite(creature.pose.yaw));

    // It asked to climb and the body cannot, so it is exactly as high as it started.
    TEST_CHECK_EQUAL(creature.pose.position.y, 0.0f);
}

TEST_CASE(a_turning_body_moves_along_the_arc_it_is_on_rather_than_the_one_it_has_left)
{
    /*
        The yaw is integrated before the step, and after a single tick the difference is exact rather
        than a matter of degree. Turning left and moving, the new heading has a negative X component,
        so the body leaves the axis immediately. Integrating the yaw *after* the step would send that
        first tick straight down -Z and leave X at exactly zero, and every later tick would trail the
        heading by one.

        Written because mutation found it: swapping the two lines left every other test in this file
        green.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_excessive", 1u};
    roster.tick(nullSenses());

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK(creature.pose.yaw > 0.0f);
    TEST_CHECK(creature.pose.position.x < 0.0f);
    TEST_CHECK(creature.pose.position.z < 0.0f);
}

TEST_CASE(a_program_that_writes_nothing_leaves_its_body_still)
{
    // The Grid zeroes the actions before every call, so silence is a stop rather than a repeat of
    // whatever was last asked for or whatever happened to be on the stack.
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_silent", 1u};

    for (uint32_t index{0u}; index < 10u; ++index) {
        roster.tick(nullSenses());
    }

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK_EQUAL(creature.pose.position.x, 0.0f);
    TEST_CHECK_EQUAL(creature.pose.position.y, 0.0f);
    TEST_CHECK_EQUAL(creature.pose.position.z, 0.0f);
    TEST_CHECK_EQUAL(creature.forward_speed, 0.0f);
}

TEST_CASE(proprioception_reports_what_the_body_did_rather_than_what_was_asked)
{
    /*
        A creature that asks for more than it has must be able to find out. The disagreement between
        the request and the report is the only way a Program learns its own limits, and reporting the
        request back would make a body indistinguishable from a faster one.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_excessive", 1u};
    roster.tick(nullSenses());

    TEST_CHECK(near(roster.creatures().front().forward_speed, MAX_FORWARD_SPEED));
    TEST_CHECK(!near(roster.creatures().front().forward_speed, 10.0f));
}

TEST_CASE(a_program_that_declines_to_rez_stops_the_roster)
{
    // A null handle is a refusal rather than a creature, and carrying on with one would mean calling
    // program_tick with a pointer the Program never issued.
    std::string message;
    try {
        RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_refuses_rez", 1u};
        roster.tick(nullSenses());
    } catch (const std::runtime_error& error) {
        message = error.what();
    }

    TEST_CHECK(!message.empty());
    TEST_CHECK(message.find("refused to rez") != std::string::npos);
}

TEST_CASE(every_creature_in_a_roster_gets_its_own_turn_and_its_own_seed)
{
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 3u};
    roster.tick(nullSenses());

    const std::vector<RosterLib::Creature>& creatures{roster.creatures()};
    TEST_CHECK_EQUAL(creatures.size(), static_cast<size_t>(3u));

    for (const RosterLib::Creature& creature : creatures) {
        TEST_CHECK(near(creature.forward_speed, 0.5f));
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

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
