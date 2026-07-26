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
#include <cstdint>
#include <vector>

/*!
    Flat entity/component scene — Structure of Arrays (SoA).

    Each entity is a uint32_t index into the parallel component arrays. No inheritance, no virtual
    functions, no heap allocation per component. The arrays map directly to GPU storage buffers,
    which is what the compute shaders read while tracing rays against the hand-built BVH.

    Materials live in a separate table rather than one per entity, because the world only ever has
    a handful of distinct surfaces (mostly-black mirrors, a few neon colours, some glass) shared by
    very many entities. Each entity stores only an index into that table.

    The scene is world state, not view state: it says nothing about cameras. The spectator camera in
    the debug window and each creature's own sensor views all trace the very same scene.
*/
class Scene {
public:
    //! Adds an entity with the given components. Returns the entity index.
    [[nodiscard]] uint32_t addEntity(const Transform& transform, const Geometry& geometry, const MaterialIndex& material_index, const Bounds& bounds)
    {
        uint32_t index{static_cast<uint32_t>(m_transforms.size())};
        m_transforms.push_back(transform);
        m_geometries.push_back(geometry);
        m_material_indices.push_back(material_index);
        m_bounds.push_back(bounds);
        return index;
    }

    //! Adds a material to the scene's material table. Returns its index, for use in a MaterialIndex component.
    [[nodiscard]] uint32_t addMaterial(const Material& material)
    {
        uint32_t index{static_cast<uint32_t>(m_materials.size())};
        m_materials.push_back(material);
        return index;
    }

    //! Returns the number of entities in the scene.
    [[nodiscard]] uint32_t entityCount() const
    {
        return static_cast<uint32_t>(m_transforms.size());
    }

    //! Returns the number of distinct materials in the scene.
    [[nodiscard]] uint32_t materialCount() const
    {
        return static_cast<uint32_t>(m_materials.size());
    }

    //! Returns the transform array (read-only).
    [[nodiscard]] const std::vector<Transform>& transforms() const
    {
        return m_transforms;
    }

    //! Returns the geometry range array (read-only).
    [[nodiscard]] const std::vector<Geometry>& geometries() const
    {
        return m_geometries;
    }

    //! Returns the per-entity material index array (read-only).
    [[nodiscard]] const std::vector<MaterialIndex>& materialIndices() const
    {
        return m_material_indices;
    }

    //! Returns the bounds array (read-only).
    [[nodiscard]] const std::vector<Bounds>& bounds() const
    {
        return m_bounds;
    }

    //! Returns the material table (read-only). Uploaded verbatim to an std430 storage buffer.
    [[nodiscard]] const std::vector<Material>& materials() const
    {
        return m_materials;
    }

    //! Mutable access to a transform (for creature movement and animation).
    [[nodiscard]] Transform& transform(uint32_t index)
    {
        return m_transforms[index];
    }

    //! Mutable access to bounds (for recalculation after a transform change).
    [[nodiscard]] Bounds& bound(uint32_t index)
    {
        return m_bounds[index];
    }

    //! Mutable access to a material (for tuning surfaces, or animating neon brightness).
    [[nodiscard]] Material& material(uint32_t index)
    {
        return m_materials[index];
    }

    //! Removes all entities and all materials.
    void clear()
    {
        m_transforms.clear();
        m_geometries.clear();
        m_material_indices.clear();
        m_bounds.clear();
        m_materials.clear();
    }

private:
    std::vector<Transform> m_transforms; //!< Per-entity spatial transforms.
    std::vector<Geometry> m_geometries; //!< Per-entity triangle ranges in the global triangle buffer.
    std::vector<MaterialIndex> m_material_indices; //!< Per-entity index into the material table.
    std::vector<Bounds> m_bounds; //!< Per-entity bounding spheres.
    std::vector<Material> m_materials; //!< Shared material table, indexed by MaterialIndex.
};
