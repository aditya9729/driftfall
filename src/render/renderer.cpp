// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/renderer.hpp"

#include "core/clock.hpp"
#include "core/log.hpp"
#include "generated/shaders/fs_chunk.sc.bin.h"
#include "generated/shaders/fs_sky.sc.bin.h"
#include "generated/shaders/vs_chunk.sc.bin.h"
#include "generated/shaders/vs_sky.sc.bin.h"
#include "render/art_direction.hpp"
#include "voxel/greedy_mesher.hpp"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace df {
namespace {

const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_chunk),
    BGFX_EMBEDDED_SHADER(fs_chunk),
    BGFX_EMBEDDED_SHADER(vs_sky),
    BGFX_EMBEDDED_SHADER(fs_sky),
    BGFX_EMBEDDED_SHADER_END(),
};

/// One fullscreen triangle in clip space. A triangle rather than a quad so
/// there is no diagonal seam where two triangles meet and no pixels shaded
/// twice along it.
struct SkyVertex {
    f32 x, y, z;
};

constexpr std::array<SkyVertex, 3> kSkyTriangle = {{
    {-1.0f, -1.0f, 0.0f},
    {3.0f, -1.0f, 0.0f},
    {-1.0f, 3.0f, 0.0f},
}};

// The art direction — every lighting constant, plus srgb_to_linear — now lives
// in render/art_direction.hpp, because the entity pass needs exactly the same
// values and a second copy of a lighting model drifts. Pulled in unqualified
// here so the call sites below read as they always did.
using namespace art;

/// Material palette, indexed by Voxel. Deliberately desaturated and cold: the
/// derelict is dead metal and ice, so the only saturated colours in frame are
/// muzzle flash, damage glow, and enemy optics. Anything that matters is the
/// only thing with colour.
constexpr std::array<f32, 8 * 4> kMaterialColors = {
    0.00f, 0.00f, 0.00f, 1.0f,  // Empty (never drawn)
    0.42f, 0.45f, 0.50f, 1.0f,  // HullPlate
    0.26f, 0.29f, 0.34f, 1.0f,  // Bulkhead
    0.62f, 0.52f, 0.28f, 1.0f,  // Ore
    0.60f, 0.72f, 0.80f, 1.0f,  // Ice
    0.35f, 0.40f, 0.36f, 1.0f,  // Barricade
    0.30f, 0.36f, 0.44f, 1.0f,  // Reinforced
    0.50f, 0.50f, 0.50f, 1.0f,  // reserved
};

}  // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialise() {
    if (initialised_) return true;

    GpuChunkMesh::init_layout();

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    program_ = bgfx::createProgram(bgfx::createEmbeddedShader(kEmbeddedShaders, type, "vs_chunk"),
                                   bgfx::createEmbeddedShader(kEmbeddedShaders, type, "fs_chunk"),
                                   true);

    if (!bgfx::isValid(program_)) {
        log::error("failed to create the chunk shader program");
        return false;
    }

    u_chunk_origin_ = bgfx::createUniform("u_chunkOrigin", bgfx::UniformType::Vec4);
    u_material_color_ = bgfx::createUniform("u_materialColor", bgfx::UniformType::Vec4, 8);
    u_light_dir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_fog_params_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    u_fog_color_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
    u_eye_pos_ = bgfx::createUniform("u_eyePos", bgfx::UniformType::Vec4);
    u_ambient_sky_ = bgfx::createUniform("u_ambientSky", bgfx::UniformType::Vec4);
    u_ambient_ground_ = bgfx::createUniform("u_ambientGround", bgfx::UniformType::Vec4);
    u_key_color_ = bgfx::createUniform("u_keyColor", bgfx::UniformType::Vec4);
    u_rim_color_ = bgfx::createUniform("u_rimColor", bgfx::UniformType::Vec4);

    // --- sky ---------------------------------------------------------------
    sky_program_ = bgfx::createProgram(bgfx::createEmbeddedShader(kEmbeddedShaders, type, "vs_sky"),
                                       bgfx::createEmbeddedShader(kEmbeddedShaders, type, "fs_sky"),
                                       true);
    if (!bgfx::isValid(sky_program_)) {
        log::error("failed to create the sky shader program");
        return false;
    }

    bgfx::VertexLayout sky_layout;
    sky_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    sky_vertices_ = bgfx::createVertexBuffer(
        bgfx::copy(kSkyTriangle.data(), static_cast<u32>(kSkyTriangle.size() * sizeof(SkyVertex))),
        sky_layout);

    u_cam_right_ = bgfx::createUniform("u_camRight", bgfx::UniformType::Vec4);
    u_cam_up_ = bgfx::createUniform("u_camUp", bgfx::UniformType::Vec4);
    u_cam_forward_ = bgfx::createUniform("u_camForward", bgfx::UniformType::Vec4);
    u_sky_horizon_ = bgfx::createUniform("u_skyHorizon", bgfx::UniformType::Vec4);
    u_sky_zenith_ = bgfx::createUniform("u_skyZenith", bgfx::UniformType::Vec4);
    u_sky_nadir_ = bgfx::createUniform("u_skyNadir", bgfx::UniformType::Vec4);

    for (usize i = 0; i < kMaterialColors.size(); ++i) {
        // Alpha stays as authored; only the colour channels are encoded.
        material_colors_linear_[i] = (i % 4 == 3) ? kMaterialColors[i] : srgb_to_linear(kMaterialColors[i]);
    }

    initialised_ = true;
    log::info("renderer ready ({})", bgfx::getRendererName(type));
    return true;
}

void Renderer::shutdown() {
    if (!initialised_) return;

    meshes_.clear();  // GpuChunkMesh destructors release their handles

    const auto destroy_uniform = [](bgfx::UniformHandle& handle) {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    };
    destroy_uniform(u_chunk_origin_);
    destroy_uniform(u_material_color_);
    destroy_uniform(u_light_dir_);
    destroy_uniform(u_fog_params_);
    destroy_uniform(u_fog_color_);
    destroy_uniform(u_eye_pos_);
    destroy_uniform(u_ambient_sky_);
    destroy_uniform(u_ambient_ground_);
    destroy_uniform(u_key_color_);
    destroy_uniform(u_rim_color_);
    destroy_uniform(u_cam_right_);
    destroy_uniform(u_cam_up_);
    destroy_uniform(u_cam_forward_);
    destroy_uniform(u_sky_horizon_);
    destroy_uniform(u_sky_zenith_);
    destroy_uniform(u_sky_nadir_);

    if (bgfx::isValid(sky_vertices_)) bgfx::destroy(sky_vertices_);
    sky_vertices_ = BGFX_INVALID_HANDLE;

    if (bgfx::isValid(sky_program_)) bgfx::destroy(sky_program_);
    sky_program_ = BGFX_INVALID_HANDLE;

    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;

    initialised_ = false;
}

void Renderer::resize(i32 width, i32 height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    bgfx::setViewRect(kMainView, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
}

void Renderer::collect_dirty(VoxelWorld& world) {
    for (const ivec3& coord : world.take_dirty_chunks()) {
        // A chunk dirtied twice before it is rebuilt only needs rebuilding
        // once. During sustained fire this collapses a lot of redundant work.
        const bool already_queued =
            std::find_if(remesh_queue_.begin(), remesh_queue_.end(), [&](const ivec3& queued) {
                return queued.x == coord.x && queued.y == coord.y && queued.z == coord.z;
            }) != remesh_queue_.end();
        if (!already_queued) remesh_queue_.push_back(coord);
    }
}

void Renderer::render(const VoxelWorld& world, const Camera& camera) {
    stats_ = RendererStats{};

    // --- pace the remeshing ------------------------------------------------
    const f64 remesh_started = now_seconds();
    const usize budget = std::min(kRemeshBudgetPerFrame, remesh_queue_.size());
    for (usize i = 0; i < budget; ++i) {
        const ivec3 coord = remesh_queue_[i];
        const ChunkMesh mesh = greedy_mesh(world, coord);
        if (mesh.empty()) {
            meshes_.erase(coord);
        } else {
            meshes_[coord].upload(mesh);
        }
        ++stats_.chunks_remeshed_this_frame;
    }
    remesh_queue_.erase(remesh_queue_.begin(), remesh_queue_.begin() + static_cast<i64>(budget));
    stats_.dirty_backlog = remesh_queue_.size();
    stats_.remesh_ms = (now_seconds() - remesh_started) * 1000.0;

    // --- draw --------------------------------------------------------------
    const glm::mat4 view = camera.view();
    const glm::mat4 proj = camera.projection();
    bgfx::setViewTransform(kMainView, &view[0][0], &proj[0][0]);
    bgfx::setViewRect(kMainView, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));
    bgfx::touch(kMainView);

    const vec3 eye = camera.eye();
    const std::array<f32, 4> fog_color = fog_color_rgba();
    const f32 eye_pos[4] = {eye.x, eye.y, eye.z, 0.0f};

    bgfx::setUniform(u_material_color_, material_colors_linear_.data(), 8);
    bgfx::setUniform(u_light_dir_, kLightDir.data());
    bgfx::setUniform(u_fog_params_, kFogParams.data());
    bgfx::setUniform(u_fog_color_, fog_color.data());
    bgfx::setUniform(u_eye_pos_, eye_pos);
    bgfx::setUniform(u_ambient_sky_, kAmbientSky.data());
    bgfx::setUniform(u_ambient_ground_, kAmbientGround.data());
    bgfx::setUniform(u_key_color_, kKeyColor.data());
    bgfx::setUniform(u_rim_color_, kRimColor.data());

    constexpr u64 kState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;

    for (const auto& [coord, mesh] : meshes_) {
        if (!mesh.valid()) continue;

        const f32 origin[4] = {static_cast<f32>(coord.x * Chunk::kSize),
                               static_cast<f32>(coord.y * Chunk::kSize),
                               static_cast<f32>(coord.z * Chunk::kSize),
                               0.0f};
        bgfx::setUniform(u_chunk_origin_, origin);
        bgfx::setVertexBuffer(0, mesh.vertex_buffer());
        bgfx::setIndexBuffer(mesh.index_buffer());
        bgfx::setState(kState);
        bgfx::submit(kMainView, program_);

        ++stats_.chunks_drawn;
        ++stats_.draw_calls;
        stats_.triangles += mesh.index_count() / 3;
    }

    draw_sky(camera);
}

void Renderer::draw_sky(const Camera& camera) {
    if (!bgfx::isValid(sky_program_) || !bgfx::isValid(sky_vertices_)) return;

    // No view transform: the triangle is already in clip space and the shader
    // builds its rays from the camera basis directly.
    bgfx::setViewRect(kSkyView, 0, 0, static_cast<u16>(width_), static_cast<u16>(height_));

    const vec3 forward = camera.forward();
    const vec3 right = camera.right();
    const vec3 up = camera.up();
    const f32 tan_half_fov = std::tan(camera.fov_y() * 0.5f);

    const f32 cam_right[4] = {right.x, right.y, right.z, tan_half_fov * camera.aspect()};
    const f32 cam_up[4] = {up.x, up.y, up.z, tan_half_fov};
    const f32 cam_forward[4] = {forward.x, forward.y, forward.z, 0.0f};
    const f32 horizon[4] = {kFogColor[0], kFogColor[1], kFogColor[2], kAmbientSky[3]};

    bgfx::setUniform(u_cam_right_, cam_right);
    bgfx::setUniform(u_cam_up_, cam_up);
    bgfx::setUniform(u_cam_forward_, cam_forward);
    bgfx::setUniform(u_sky_horizon_, horizon);
    bgfx::setUniform(u_sky_zenith_, kSkyZenith.data());
    bgfx::setUniform(u_sky_nadir_, kSkyNadir.data());

    // Depth test but no depth write: the sky loses to anything the sector
    // already drew, and never occludes anything drawn after it. No culling,
    // because a fullscreen triangle's winding is whatever the vertex order
    // happens to give.
    bgfx::setVertexBuffer(0, sky_vertices_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL);
    bgfx::submit(kSkyView, sky_program_);

    ++stats_.draw_calls;
}

}  // namespace df
