// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/job_system.hpp"
#include "render/camera.hpp"
#include "render/chunk_mesh.hpp"
#include "voxel/voxel_world.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <unordered_map>

namespace df {

struct RendererStats {
    u32 chunks_drawn = 0;
    u32 chunks_remeshed_this_frame = 0;
    u32 draw_calls = 0;
    u32 triangles = 0;
    usize dirty_backlog = 0;

    /// Wall-clock milliseconds spent rebuilding chunk meshes this frame. The
    /// chunk count alone does not tell you whether the remesh budget was met —
    /// four trivial chunks and four dense ones are the same count and wildly
    /// different costs — so the HUD needs the time, not just the tally.
    f64 remesh_ms = 0.0;
};

/// Draws the voxel world.
///
/// The renderer's real job is not drawing — it is *pacing remeshing*. A single
/// shotgun blast can dirty four chunks at once; rebuilding all of them in the
/// frame they were dirtied is a guaranteed hitch. So dirty chunks queue, a
/// bounded number are rebuilt per frame off the main thread, and the world is
/// briefly one frame stale rather than briefly at 20 fps.
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Creates GPU resources. Must be called after bgfx::init.
    bool initialise();

    void shutdown();

    void resize(i32 width, i32 height);

    /// Pulls newly dirtied chunks out of the world and queues them.
    void collect_dirty(VoxelWorld& world);

    /// Rebuilds up to kRemeshBudgetPerFrame queued chunks and draws the world.
    void render(const VoxelWorld& world, const Camera& camera);

    [[nodiscard]] const RendererStats& stats() const { return stats_; }

    /// How many chunks may be rebuilt in one frame. Four is the measured point
    /// where remeshing still fits under 2.5 ms on an iPhone 12 alongside
    /// everything else. Raise it only with a profiler open.
    static constexpr usize kRemeshBudgetPerFrame = 4;

    static constexpr bgfx::ViewId kMainView = 0;

    /// The sky is drawn *after* the sector, in a later view, with a depth test
    /// against what the sector already wrote. On a tiler that rejects almost
    /// every pixel before the sky shader runs — inside the hull the open
    /// ceiling is a sliver of the screen — where a background pass would have
    /// shaded the whole frame and then thrown it away.
    static constexpr bgfx::ViewId kSkyView = 1;

private:
    struct ChunkHash {
        usize operator()(const ivec3& c) const noexcept {
            return (static_cast<usize>(static_cast<u32>(c.x)) * 73856093u) ^
                   (static_cast<usize>(static_cast<u32>(c.y)) * 19349663u) ^
                   (static_cast<usize>(static_cast<u32>(c.z)) * 83492791u);
        }
    };

    struct ChunkEq {
        bool operator()(const ivec3& a, const ivec3& b) const noexcept {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
    };

    void draw_sky(const Camera& camera);

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_chunk_origin_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_material_color_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_light_dir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fog_params_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fog_color_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_eye_pos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ambient_sky_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ambient_ground_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_key_color_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_rim_color_ = BGFX_INVALID_HANDLE;

    bgfx::ProgramHandle sky_program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle sky_vertices_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cam_right_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cam_up_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cam_forward_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sky_horizon_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sky_zenith_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sky_nadir_ = BGFX_INVALID_HANDLE;

    /// The material palette converted to linear once at startup. The table is
    /// authored in display space because that is the only way to read it, but
    /// every light in the shader is linear, so mixing the two is what makes
    /// flat-shaded surfaces look plastic.
    std::array<f32, 8 * 4> material_colors_linear_{};

    std::unordered_map<ivec3, GpuChunkMesh, ChunkHash, ChunkEq> meshes_;
    std::vector<ivec3> remesh_queue_;
    RendererStats stats_;
    i32 width_ = 1280;
    i32 height_ = 720;
    bool initialised_ = false;
};

}  // namespace df
