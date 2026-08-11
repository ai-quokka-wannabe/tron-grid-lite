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
        desc.max_vocalisation_strength = 1.0f;
        return desc;
    }

    //! The terrain every behaviour test stands on unless it says otherwise: flat, at height zero.
    [[nodiscard]] RosterLib::GroundFunction flatGround()
    {
        return [](float, float) {
            return 0.0f;
        };
    }

    //! Where a body's origin sits when standing on flat ground.
    constexpr float STANDING_Y{RosterLib::BODY_HALF_HEIGHT};

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
    // voiceless body asking to call is told no by the same mechanism that tells it how loud it may.
    TglCreatureDesc voiceless{boundedBody()};
    voiceless.max_vocalisation_strength = 0.0f;

    TglActions actions{};
    actions.vocalisation_strength = 5.0f;

    RosterLib::sanitiseAndClamp(actions, voiceless);

    TEST_CHECK(near(actions.vocalisation_strength, 0.0f));
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
    actions.vocalisation_strength = std::numeric_limits<float>::quiet_NaN();

    RosterLib::sanitiseAndClamp(actions, boundedBody());

    TEST_CHECK(std::isfinite(actions.desired_forward_speed));
    TEST_CHECK(std::isfinite(actions.desired_turn_rate));
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

        Thirty-three ticks for those thirty-two steps, and the difference is the lifecycle: an
        action takes effect on the next tick, so the first tick stages the intent and the second is
        the first that moves. A test that forgot this would be measuring the staging delay and
        calling it drift.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, flatGround()};

    for (uint32_t index{0u}; index < (RosterLib::TICKS_PER_SECOND + 1u); ++index) {
        roster.tick(nullSenses());
    }

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK_EQUAL(roster.tickCount(), static_cast<uint64_t>(RosterLib::TICKS_PER_SECOND) + 1u);
    TEST_CHECK_EQUAL(creature.pose.position.z, -0.5f);
    TEST_CHECK_EQUAL(creature.pose.position.x, 0.0f);
    TEST_CHECK_EQUAL(creature.pose.position.y, STANDING_Y);
    TEST_CHECK_EQUAL(creature.pose.yaw, 0.0f);
}

TEST_CASE(a_program_asking_for_more_than_its_body_has_gets_the_body)
{
    /*
        Two ticks with a Program that asks for ten metres a second, a NaN turn and a negative shout:
        one to stage the intent, one for physics to act on it. What arrives at the actuators is the
        body's own limits, and nothing that is not a number reaches the pose.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_excessive", 1u, flatGround()};
    roster.tick(nullSenses());
    roster.tick(nullSenses());

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK(near(creature.forward_speed, MAX_FORWARD_SPEED));

    // A NaN request becomes zero rather than a bound: the creature walks straight, not in the
    // tightest circle its body allows.
    TEST_CHECK_EQUAL(creature.turn_rate, 0.0f);

    TEST_CHECK(std::isfinite(creature.pose.position.x));
    TEST_CHECK(std::isfinite(creature.pose.position.y));
    TEST_CHECK(std::isfinite(creature.pose.position.z));
    TEST_CHECK(std::isfinite(creature.pose.yaw));

    // Nothing lifts a walking body off flat ground.
    TEST_CHECK_EQUAL(creature.pose.position.y, STANDING_Y);
}

TEST_CASE(a_turning_body_moves_along_the_exact_arc)
{
    /*
        A body walking at constant speed while turning at constant rate traces a circle, and the
        step integrates it in closed form — so the position after any number of ticks must lie on
        that circle exactly, not near it. The circle's centre sits a radius to the body's left of
        where it started: at s = 0.5 m/s and ω = 0.78539816 rad/s the radius is s/ω, and turning
        left from a -Z heading puts the centre at (-r, 0, 0).

        A chord-walking integrator — turn, then move straight — drifts off this circle a little
        every tick, always outward. The assertion is on the radius staying put, which is the
        property the closed form has and the chord does not.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_turning", 1u, flatGround()};

    constexpr float SPEED{0.5f};
    constexpr float TURN{0.78539816f};
    constexpr float RADIUS{SPEED / TURN};

    for (uint32_t index{0u}; index < 64u; ++index) {
        roster.tick(nullSenses());
    }

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK(creature.pose.yaw > 0.0f);
    TEST_CHECK(creature.pose.position.x < 0.0f);

    const float dx{creature.pose.position.x - (-RADIUS)};
    const float dz{creature.pose.position.z - 0.0f};
    TEST_CHECK_CLOSE(std::sqrt((dx * dx) + (dz * dz)), RADIUS, 1e-4f);
}

TEST_CASE(a_program_that_writes_nothing_leaves_its_body_still)
{
    // The Grid zeroes the actions before every call, so silence is a stop rather than a repeat of
    // whatever was last asked for or whatever happened to be on the stack.
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_silent", 1u, flatGround()};

    for (uint32_t index{0u}; index < 10u; ++index) {
        roster.tick(nullSenses());
    }

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK_EQUAL(creature.pose.position.x, 0.0f);
    TEST_CHECK_EQUAL(creature.pose.position.y, STANDING_Y);
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
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_excessive", 1u, flatGround()};
    roster.tick(nullSenses());
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

// ---------------------------------------------------------------------------------------------
// Gravity, contacts, friction
// ---------------------------------------------------------------------------------------------

TEST_CASE(a_standing_body_feels_the_floor_every_tick)
{
    /*
        The support impulse is not an event, it is the floor holding the body up: mass times gravity
        times the tick, under the feet, every tick the body stands. Exact, because every factor is
        representable and the product is one multiply.
    */
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_silent", 1u, flatGround()};
    roster.tick(nullSenses());

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK_EQUAL(creature.contacts.size(), static_cast<size_t>(1u));
    TEST_CHECK_EQUAL(creature.contacts[0].position[1], -RosterLib::BODY_HALF_HEIGHT);
    TEST_CHECK_CLOSE(creature.contacts[0].impulse[1], RosterLib::BODY_MASS_KG * RosterLib::GRAVITY * RosterLib::TICK_SECONDS, 1e-6f);

    // At rest the otolith reads gravity pointing up through the body: the floor pushing back.
    TEST_CHECK_CLOSE(creature.specific_force.y, RosterLib::GRAVITY, 1e-4f);
    TEST_CHECK_CLOSE(creature.specific_force.x, 0.0f, 1e-4f);
    TEST_CHECK_CLOSE(creature.specific_force.z, 0.0f, 1e-4f);
}

TEST_CASE(a_body_that_walks_off_an_edge_falls_and_feels_nothing)
{
    // A cliff across the path: solid ground until z = -0.3, a five-metre drop beyond it.
    RosterLib::GroundFunction cliff{[](float, float z) {
        return (z > -0.3f) ? 0.0f : -5.0f;
    }};

    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, cliff};

    // At half a metre a second the edge is about 20 ticks out; by 40 the body is over it.
    for (uint32_t index{0u}; index < 40u; ++index) {
        roster.tick(nullSenses());
    }

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK(!creature.grounded);
    TEST_CHECK(creature.velocity.y < 0.0f);
    TEST_CHECK(creature.pose.position.y < STANDING_Y);

    // Falling is the one state in which a body touches nothing and feels nothing: no contacts, and
    // an otolith reading of zero, which is why falling reads as weightless.
    TEST_CHECK_EQUAL(creature.contacts.size(), static_cast<size_t>(0u));
    const float felt{std::sqrt((creature.specific_force.x * creature.specific_force.x) + (creature.specific_force.y * creature.specific_force.y)
        + (creature.specific_force.z * creature.specific_force.z))};
    TEST_CHECK(felt < 0.1f);
}

TEST_CASE(a_falling_body_lands_with_a_thump_the_floor_alone_cannot_explain)
{
    RosterLib::GroundFunction cliff{[](float, float z) {
        return (z > -0.3f) ? 0.0f : -5.0f;
    }};

    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, cliff};

    // Long enough to walk off and complete the metre of fall: a metre takes about 0.45 s, so by
    // 96 ticks the body has been standing on the lower terrace for a while.
    bool landed_hard{false};
    const float support_alone{RosterLib::BODY_MASS_KG * RosterLib::GRAVITY * RosterLib::TICK_SECONDS};
    for (uint32_t index{0u}; index < 96u; ++index) {
        roster.tick(nullSenses());
        const RosterLib::Creature& creature{roster.creatures().front()};
        for (const TglContact& contact : creature.contacts) {
            if (contact.impulse[1] > (support_alone * 2.0f)) {
                landed_hard = true;
            }
        }
    }

    const RosterLib::Creature& creature{roster.creatures().front()};
    TEST_CHECK(landed_hard);
    TEST_CHECK(creature.grounded);
    TEST_CHECK_CLOSE(creature.pose.position.y, -5.0f + RosterLib::BODY_HALF_HEIGHT, 1e-3f);
}

TEST_CASE(a_terrace_riser_is_a_wall_the_body_feels_on_its_face)
{
    // A step up taller than ankle height, across the path.
    RosterLib::GroundFunction step{[](float, float z) {
        return (z > -0.3f) ? 0.0f : 1.0f;
    }};

    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_steady", 1u, step};

    bool felt_wall{false};
    for (uint32_t index{0u}; index < 40u; ++index) {
        roster.tick(nullSenses());
        const RosterLib::Creature& creature{roster.creatures().front()};
        for (const TglContact& contact : creature.contacts) {
            if ((contact.position[2] == -RosterLib::BODY_HALF_LENGTH) && (contact.impulse[2] > 0.0f)) {
                felt_wall = true;
            }
        }
    }

    const RosterLib::Creature& creature{roster.creatures().front()};

    TEST_CHECK(felt_wall);

    // Stopped at the wall rather than climbing it or passing through, still on the low side.
    TEST_CHECK(creature.pose.position.z > -0.35f);
    TEST_CHECK_EQUAL(creature.pose.position.y, STANDING_Y);
    TEST_CHECK_EQUAL(creature.forward_speed, 0.0f);
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

TEST_CASE(a_meaningless_loudness_reaches_the_voice_as_silence)
{
    // The excessive Program shouts at minus five. A negative loudness is not a quieter sound but
    // a meaningless one, so what reaches the voice actuator is silence rather than a magnitude.
    RosterLib::Roster roster{fixtureDirectory(), "tgl_driver_excessive", 1u, flatGround()};
    roster.tick(nullSenses());
    roster.tick(nullSenses());

    TEST_CHECK_EQUAL(roster.creatures().front().vocalisation, 0.0f);
}

TEST_CASE(the_same_run_hashes_bit_identically_twice)
{
    /*
        Etape 16's determinism check, firing on every push with no device: the world tick is claimed
        reproducible on one build and one machine, so two identical rosters over identical terrain
        must produce bit-identical state at every tick. FNV-1a over the bytes of every body's pose,
        velocity and actuators — a hash because the failure this hunts is a single stray bit from
        uninitialised state or order-dependent arithmetic, which a tolerance would forgive.

        Cross-platform golden values are deliberately not asserted: yaw goes through sin and cos,
        and no two libms agree in the last bit. The claim is per build, and so is the check.
    */
    const auto hashRoster = [](RosterLib::Roster& roster) {
        uint64_t hash{14695981039346656037ull};
        const auto mix = [&hash](float value) {
            uint32_t bits{0u};
            std::memcpy(&bits, &value, sizeof(bits));
            for (uint32_t byte{0u}; byte < 4u; ++byte) {
                hash ^= (bits >> (byte * 8u)) & 0xFFu;
                hash *= 1099511628211ull;
            }
        };
        for (const RosterLib::Creature& creature : roster.creatures()) {
            mix(creature.pose.position.x);
            mix(creature.pose.position.y);
            mix(creature.pose.position.z);
            mix(creature.pose.yaw);
            mix(creature.velocity.x);
            mix(creature.velocity.y);
            mix(creature.velocity.z);
            mix(creature.forward_speed);
            mix(creature.turn_rate);
            mix(creature.vocalisation);
        }
        return hash;
    };

    RosterLib::GroundFunction terraced{[](float, float z) {
        return (z > -1.0f) ? 0.0f : -0.5f;
    }};

    RosterLib::Roster first{fixtureDirectory(), "tgl_driver_turning", 3u, terraced};
    RosterLib::Roster second{fixtureDirectory(), "tgl_driver_turning", 3u, terraced};

    uint64_t first_hash_at_start{0u};
    bool anything_moved{false};

    for (uint32_t index{0u}; index < 128u; ++index) {
        first.tick(nullSenses());
        second.tick(nullSenses());

        const uint64_t first_hash{hashRoster(first)};
        const uint64_t second_hash{hashRoster(second)};
        TEST_CHECK_EQUAL(first_hash, second_hash);

        if (index == 0u) {
            first_hash_at_start = first_hash;
        } else if (first_hash != first_hash_at_start) {
            anything_moved = true;
        }
    }

    // The floor under the comparison: two frozen worlds also agree perfectly, about nothing.
    TEST_CHECK(anything_moved);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
