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

#include "components.hpp"
#include "roster.hpp"
#include "world_client.hpp"

#include <bvh/bvh.hpp>
#include <math/matrix.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

/*!
    The Grid with its creatures standing on it: one scene, every body placed, every material
    accounted for.

    This is where an accepted `CreatureModel` becomes something rays can strike. At construction
    each modelled creature's shape is built into its own hierarchy — once, because a rigid body's
    hierarchy never needs rebuilding — and placed as an instance beside the Grid's own, exactly the
    arrangement the two-level structure was built for. Each tick, `update` moves the instances to
    the poses physics settled; nothing is ever rebuilt, only re-placed.

    The material table is likewise one table: the Grid's six slots first, then each creature's
    materials appended in roster order, with every body triangle's index rewritten to its global
    slot at construction. One table rather than per-body tables, because a triangle's material
    index is the only key the tracers have, and two numbering schemes sharing one buffer is the
    duplicated-fact defect this repository keeps finding. The acoustic strengths extend the same
    way, explicitly to zero: a body reflects sound and does not sing — a creature's voice is its
    vocalisation, not its hull.

    No Vulkan here, deliberately: the scene this class owns is the one the host acoustics walk
    under ctest, and the flat records it emits are what a device consumer uploads. What stands
    behind each is not this class's business.
*/
class Stage {
public:
    /*!
        Assembles the scene: the Grid at the identity, then one instance per modelled creature.

        \param grid The Grid's own hierarchy. Taken by value and kept: the scene owns its
               geometries, and a borrowed Grid would be two owners of one lifetime.
        \param grid_materials The Grid's material table, which creature materials are appended to.
        \param creatures The roster, already rezzed, models validated. Bodiless creatures take no
               instance and cost nothing.
    */
    Stage(BvhLib::Bvh grid, std::vector<Material> grid_materials, const std::vector<RosterLib::Creature>& creatures);

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    Stage(Stage&&) = delete;
    Stage& operator=(Stage&&) = delete;

    /*!
        Moves every body's instance to the pose physics settled.

        Called once per tick, after physics and before any sense is filled — the same moment
        `SensesSource::beginTick` exists for, and the reason it exists. The transform is the pose's
        own yaw-then-translate, so a point of the body lands exactly where `worldFromBody` says it
        does; the two are held together by a test rather than by hope.

        \param creatures The same roster, in the same order, that construction saw.
    */
    void update(const std::vector<RosterLib::Creature>& creatures);

    /*!
        A creature this host does not drive, as the world tells it: where it stands and whether
        it is calling. Guests are what the hosted creatures' senses meet - the other bodies in
        the world, shaped by their own hosts' REZ and relayed by Master Control.
    */
    struct GuestTelling {
        uint32_t creature_id{0u};
        RosterLib::Pose pose{};
        MathLib::Vec3 velocity{};
        float vocalisation{0.0f};
    };

    /*!
        The guests' shapes, replacing whatever guests were set before: one geometry per shaped
        body in creature order, appended after the hosted bodies' own, its materials appended to
        the table. Rare - a REZ or a DEREZ with rows - and the caller rebuilds the device's world
        and the tracers that hold it afterwards, because the concatenated buffers changed. A
        guest whose REZ carried no rows stands nowhere and is never seen or occluded.
    */
    void setGuests(const std::unordered_map<uint32_t, WorldClientLib::Body>& bodies);

    //! Moves every shaped guest's instance to the pose the world told. Called each tick.
    void placeGuests(const std::vector<GuestTelling>& guests);

    //! The instance a shaped guest stands in, or `BvhLib::NO_INSTANCE` for one with no shape.
    [[nodiscard]] uint32_t guestInstanceOf(uint32_t creature_id) const noexcept;

    //! The scene, for the host tracers. Instances move under `update`; geometries change only under `setGuests`.
    [[nodiscard]] const BvhLib::Scene& scene() const noexcept
    {
        return m_scene;
    }

    //! The combined material table: the Grid's slots, then every body's, in roster order.
    [[nodiscard]] const std::vector<Material>& materials() const noexcept
    {
        return m_materials;
    }

    //! Acoustic source strength per combined slot: the Grid's authored table, and zero for every
    //! body slot — a hull reflects and does not sing.
    [[nodiscard]] const std::vector<float>& acousticStrengths() const noexcept
    {
        return m_acoustic_strengths;
    }

    //! The instance a creature's body stands in, or `BvhLib::NO_INSTANCE` for a bodiless one.
    //! This is what a creature's own senses pass as their skip.
    [[nodiscard]] uint32_t instanceOf(uint64_t creature_id) const noexcept;

    //! The whole scene in upload form. Construction-time work: a consumer keeps the nodes and
    //! triangles, and refreshes only what `flatInstances` returns.
    [[nodiscard]] BvhLib::FlatScene flatten() const
    {
        return BvhLib::flatten(m_scene);
    }

    /*!
        The current instance records alone, offsets pre-resolved.

        The per-tick shape of `flatten`: geometry never moves in the shared buffers, so a tick owes
        the device only these 144-byte records. Built through the same `flattenInstance` that
        `flatten` itself uses, which is what keeps the cheap path and the whole path in step.
    */
    [[nodiscard]] std::vector<BvhLib::InstanceRecord> flatInstances() const;

private:
    //! One modelled creature's standing in the scene.
    struct Body {
        uint64_t creature_id{0u};
        uint32_t instance{BvhLib::NO_INSTANCE}; //!< Index into the scene's instances.
        uint32_t geometry{0u}; //!< Index into the scene's geometries.
    };

    BvhLib::Scene m_scene;
    std::vector<Material> m_materials;
    std::vector<float> m_acoustic_strengths;
    std::vector<Body> m_bodies; //!< Modelled creatures only, in roster order.
    std::vector<Body> m_guests; //!< Shaped guests, in creature order, after the hosted bodies.
    size_t m_own_geometry_count{0u}; //!< Geometries setGuests keeps: the Grid and the hosted bodies.
    size_t m_own_material_count{0u};
    size_t m_own_instance_count{0u};
    void cacheOffsets();

    //! Where each geometry landed in the concatenated buffers, cached from construction so that
    //! per-tick records need no re-concatenation.
    std::vector<uint32_t> m_node_offsets;
    std::vector<uint32_t> m_triangle_offsets;
};

//! The world transform of a pose: yaw about +Y, then translation — the matrix twin of
//! `RosterLib::worldFromBody`, and the tests hold the two to each other.
[[nodiscard]] MathLib::Mat4 poseTransform(const RosterLib::Pose& pose);
