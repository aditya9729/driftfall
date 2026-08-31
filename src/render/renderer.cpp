// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/renderer.hpp"

#include "core/clock.hpp"
#include "core/log.hpp"
#include "generated/shaders/fs_chunk.sc.bin.h"
#include "generated/shaders/vs_chunk.sc.bin.h"
#include "voxel/greedy_mesher.hpp"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <array>

namespace df {
namespace {

const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_chunk),
    BGFX_EMBEDDED_SHADER(fs_chunk),
    BGFX_EMBEDDED_SHADER_END(),
};

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
    const f32 light_dir[4] = {-0.42f, -0.78f, -0.46f, 0.0f};
    const f32 fog_params[4] = {70.0f, 180.0f, 0.0f, 0.0f};
    const f32 fog_color[4] = {0.035f, 0.043f, 0.062f, 1.0f};
    const f32 eye_pos[4] = {eye.x, eye.y, eye.z, 0.0f};

    bgfx::setUniform(u_material_color_, kMaterialColors.data(), 8);
    bgfx::setUniform(u_light_dir_, light_dir);
    bgfx::setUniform(u_fog_params_, fog_params);
    bgfx::setUniform(u_fog_color_, fog_color);
    bgfx::setUniform(u_eye_pos_, eye_pos);

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
}

}  // namespace df
