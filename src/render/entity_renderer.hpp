// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "render/camera.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <span>

namespace df {

/// One box to draw this frame.
///
/// Deliberately a flat POD with no simulation types in it. The renderer must
/// not know what an EnemyKind is: enemies, turrets, dropped salvage and debug
/// markers are all "a box with a size and a colour", and the moment this struct
/// names one of them the render path starts growing gameplay branches.
struct EntityInstance {
    vec3 position{0.0f};

    /// Half-size along each local axis, *before* yaw. Silhouette is gameplay
    /// information in this game — a player being charged has to tell a Bulwark
    /// from a Skitter in a glance — so the shape is entirely the caller's to
    /// choose. Nothing about the size is baked into the shader or the cube.
    vec3 half_extents{0.5f};

    /// Rotation about +Y, in radians, matching Camera's convention: yaw 0 faces
    /// +Z. Only yaw, because these are machines walking on a deck and a full
    /// orientation would cost three more floats per instance for a pitch and
    /// roll nothing in the sim produces.
    f32 yaw = 0.0f;

    /// Index into the renderer's own colour table; see EntityPalette. Out of
    /// range clamps to the last slot rather than reading past the table.
    u32 palette_index = 0;

    /// 0..1 hit response, driven by the caller's own decay. Shooting something
    /// that does not visibly react feels dead, and at 220 bodies the flash is
    /// often the only way to tell which one you actually hit.
    f32 damage_flash = 0.0f;
};

/// Colour identities, not simulation types.
///
/// The names are the intended mapping and nothing more — EnemyKind lives in
/// src/game and is deliberately not included here. The integration layer owns
/// the EnemyKind -> palette slot translation.
///
/// The chassis colours are cold and desaturated to sit inside the derelict's
/// palette; the *optic* carries the saturation, per DESIGN.md's rule that the
/// only saturated colour in frame is muzzle flash, damage glow and enemy
/// optics. Hostile optics are warm, the player's turret is cold: that hue split
/// is the fastest friend/foe read available in a dark corridor.
enum class EntityPalette : u32 {
    Skitter = 0,
    Lancer = 1,
    Bulwark = 2,
    Breacher = 3,
    Warden = 4,
    Turret = 5,     ///< Player-built. Cold optic.
    Structure = 6,  ///< Player-built non-turret props. Cold optic.
    Debug = 7,      ///< Flat grey. Also the clamp target for a bad index.
};

struct EntityRendererStats {
    u32 entities_drawn = 0;

    /// Instances that did not fit in this frame's transient instance buffer.
    /// Silent dropping is how a horde renderer ends up "mysteriously" missing
    /// enemies at wave 40, so it is counted rather than ignored.
    u32 entities_dropped = 0;

    u32 draw_calls = 0;
    u32 triangles = 0;
};

/// Draws every non-voxel body in the sector as an instanced box.
///
/// One static unit cube, one draw call per batch. The alternative — a mesh per
/// entity, or a vertex buffer rebuilt each frame — is what makes 220 bodies
/// cost 220 draw calls, and on a mobile tiler the draw call is the expensive
/// half, not the triangles.
///
/// Owns its bgfx handles and releases them in the destructor, matching
/// GpuChunkMesh: this engine leaks buffers the moment ownership is ambiguous.
class EntityRenderer {
public:
    EntityRenderer() = default;
    ~EntityRenderer();

    EntityRenderer(const EntityRenderer&) = delete;
    EntityRenderer& operator=(const EntityRenderer&) = delete;
    EntityRenderer(EntityRenderer&& other) noexcept;
    EntityRenderer& operator=(EntityRenderer&& other) noexcept;

    /// Creates the cube, the program and the uniforms. Must be called after
    /// bgfx::init, because it reads bgfx::getCaps().
    bool initialise();

    void shutdown();

    /// Draws every instance into `view`.
    ///
    /// The camera is needed for more than the transform: fog, specular and rim
    /// are all eye-relative, and handing in the camera is cheaper than a second
    /// uniform the caller has to remember to keep in sync.
    void draw(bgfx::ViewId view, const Camera& camera, std::span<const EntityInstance> instances);

    /// Covers the most recent draw() only — draw() resets them — so a caller
    /// that splits entities across two passes has to sum them itself.
    [[nodiscard]] const EntityRendererStats& stats() const { return stats_; }

    /// False means every entity costs its own draw call this run. Worth putting
    /// on the debug HUD — it is a 200x difference in draw calls and there is no
    /// other symptom.
    [[nodiscard]] bool instancing_supported() const { return instancing_; }

    static constexpr usize kPaletteSize = 8;

    /// 4 x vec4. bgfx requires an instance stride that is a multiple of 16, and
    /// four is what the layout below needs; see write_instance().
    static constexpr u16 kInstanceStride = 64;

private:
    /// Every bgfx handle in one aggregate so the move operations are a single
    /// copy plus a single reset, instead of thirteen hand-written lines that
    /// silently rot every time a uniform is added.
    struct Gpu {
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle cube_vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle cube_indices = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_entity_instance = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_light_dir = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fog_params = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fog_color = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_eye_pos = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_ambient_sky = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_ambient_ground = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_key_color = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_rim_color = BGFX_INVALID_HANDLE;
    };

    /// One palette slot, already converted to linear and with the optic's
    /// intensity folded into its colour. Resolving on the CPU keeps the shader
    /// free of a uniform array indexed by a per-instance value, which is the
    /// part of the chunk shader most likely to bite on a weak GLES3 driver.
    struct PaletteSlot {
        std::array<f32, 3> chassis{};
        std::array<f32, 3> optic{};
    };

    /// Writes one instance as 16 floats. Shared by both paths so the instanced
    /// stream and the fallback uniform can never disagree about the layout.
    void write_instance(std::array<f32, 16>& out, const EntityInstance& entity) const;

    /// Sets everything that is constant for the frame. Uniform values in bgfx
    /// are consumed by submit(), so this runs before *every* submit rather than
    /// once at the top of draw().
    void set_frame_uniforms(const Camera& camera) const;

    Gpu gpu_;
    std::array<PaletteSlot, kPaletteSize> palette_linear_{};
    EntityRendererStats stats_;
    bool instancing_ = false;
    bool initialised_ = false;
};

}  // namespace df
