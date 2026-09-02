// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/entity_renderer.hpp"

#include "core/log.hpp"
#include "generated/shaders/fs_entity.sc.bin.h"
#include "generated/shaders/vs_entity.sc.bin.h"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace df {
namespace {

const bgfx::EmbeddedShader kEmbeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_entity),
    BGFX_EMBEDDED_SHADER(fs_entity),
    BGFX_EMBEDDED_SHADER_END(),
};

f32 srgb_to_linear(f32 c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// --- art direction ---------------------------------------------------------
// DUPLICATED from render/renderer.cpp, which is the wrong place for them to
// live now that a second pass needs them. They are in an anonymous namespace
// there, so there is no way to reference them from here without editing that
// file. The values below must stay byte-identical to renderer.cpp's or enemies
// will be lit differently from the world and read as pasted on — which is
// exactly the drift a shared header prevents. See the note in the handover:
// these want hoisting into src/render/art_direction.hpp and including from
// both.

constexpr std::array<f32, 4> kAmbientSky = {0.050f, 0.072f, 0.120f, 1.0f};
constexpr std::array<f32, 4> kAmbientGround = {0.012f, 0.013f, 0.018f, 0.0f};
constexpr std::array<f32, 4> kKeyColor = {1.0f, 0.94f, 0.85f, 1.05f};
constexpr std::array<f32, 4> kRimColor = {0.16f, 0.28f, 0.50f, 0.10f};
constexpr std::array<f32, 3> kFogColor = {0.020f, 0.028f, 0.048f};

/// Direction the key light travels. Matches Renderer::render().
constexpr std::array<f32, 4> kLightDir = {-0.42f, -0.78f, -0.46f, 0.0f};

/// x = distance fog starts, y = density. Matches Renderer::render().
constexpr std::array<f32, 4> kFogParams = {18.0f, 0.016f, 0.0f, 0.0f};

// --- palette ---------------------------------------------------------------

struct PaletteEntry {
    std::array<f32, 3> chassis;  ///< display space, converted at init
    std::array<f32, 3> optic;    ///< display space, converted at init
    f32 optic_intensity;         ///< linear multiplier folded in at init
};

/// Chassis colours stay inside the derelict's cold, desaturated range so the
/// machines belong to the station; the optic carries every bit of saturation.
/// Hostiles read warm, the player's own hardware reads cold — against a blue
/// sector that hue split is legible before the silhouette resolves.
constexpr std::array<PaletteEntry, EntityRenderer::kPaletteSize> kPalette = {{
    {{0.32f, 0.35f, 0.37f}, {1.00f, 0.45f, 0.18f}, 1.7f},  // Skitter
    {{0.25f, 0.28f, 0.34f}, {1.00f, 0.68f, 0.22f}, 2.3f},  // Lancer
    {{0.44f, 0.43f, 0.39f}, {1.00f, 0.26f, 0.15f}, 2.0f},  // Bulwark
    {{0.38f, 0.29f, 0.20f}, {1.00f, 0.32f, 0.06f}, 3.2f},  // Breacher — it is a bomb, say so
    {{0.17f, 0.18f, 0.22f}, {0.92f, 0.18f, 0.30f}, 3.6f},  // Warden
    {{0.36f, 0.42f, 0.44f}, {0.36f, 0.88f, 1.00f}, 2.1f},  // Turret (player)
    {{0.30f, 0.36f, 0.44f}, {0.36f, 0.88f, 1.00f}, 1.0f},  // Structure (player)
    {{0.50f, 0.50f, 0.50f}, {1.00f, 1.00f, 1.00f}, 0.0f},  // Debug
}};

// --- the one cube ----------------------------------------------------------

/// w is the face's normal index, encoded exactly as greedy_mesher.cpp encodes
/// it — axis * 2 + (positive ? 0 : 1) — so vs_entity can unpack it with the
/// same three lines vs_chunk already uses. Carrying the normal in the spare
/// component of a float attribute rather than as a second attribute is not
/// premature packing: a second *integer* attribute is the WebGL2 trap
/// documented in chunk_mesh.cpp, and a second float attribute would be 12
/// bytes to say one of six things.
struct CubeVertex {
    f32 x, y, z;
    f32 normal_index;
};

/// Half-extent 1, not 0.5. The instance's half_extents then multiply straight
/// in with no stray factor of two, and the interpolated local position arrives
/// in the fragment shader already normalised to -1..1 whatever the box's real
/// size is. That is what lets one cube serve a Skitter and a Warden.
///
/// Winding matches the mesher: corners ordered so (c1-c0) x (c2-c0) points
/// along the outward normal, triangles (0,1,2) and (0,2,3), which is what
/// BGFX_STATE_CULL_CW expects here.
constexpr std::array<CubeVertex, 24> kCubeVertices = {{
    // +X
    {1.0f, -1.0f, -1.0f, 0.0f},
    {1.0f, 1.0f, -1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f},
    {1.0f, -1.0f, 1.0f, 0.0f},
    // -X
    {-1.0f, -1.0f, -1.0f, 1.0f},
    {-1.0f, -1.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f, -1.0f, 1.0f},
    // +Y
    {-1.0f, 1.0f, -1.0f, 2.0f},
    {-1.0f, 1.0f, 1.0f, 2.0f},
    {1.0f, 1.0f, 1.0f, 2.0f},
    {1.0f, 1.0f, -1.0f, 2.0f},
    // -Y
    {-1.0f, -1.0f, -1.0f, 3.0f},
    {1.0f, -1.0f, -1.0f, 3.0f},
    {1.0f, -1.0f, 1.0f, 3.0f},
    {-1.0f, -1.0f, 1.0f, 3.0f},
    // +Z
    {-1.0f, -1.0f, 1.0f, 4.0f},
    {1.0f, -1.0f, 1.0f, 4.0f},
    {1.0f, 1.0f, 1.0f, 4.0f},
    {-1.0f, 1.0f, 1.0f, 4.0f},
    // -Z
    {-1.0f, -1.0f, -1.0f, 5.0f},
    {-1.0f, 1.0f, -1.0f, 5.0f},
    {1.0f, 1.0f, -1.0f, 5.0f},
    {1.0f, -1.0f, -1.0f, 5.0f},
}};

constexpr std::array<u16, 36> kCubeIndices = {{
    0,  1,  2,  0,  2,  3,   // +X
    4,  5,  6,  4,  6,  7,   // -X
    8,  9,  10, 8,  10, 11,  // +Y
    12, 13, 14, 12, 14, 15,  // -Y
    16, 17, 18, 16, 18, 19,  // +Z
    20, 21, 22, 20, 22, 23,  // -Z
}};

/// Identical to the chunk pass. Entities that write depth differently from the
/// world are how you get enemies visible through walls.
constexpr u64 kDrawState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;

}  // namespace

EntityRenderer::~EntityRenderer() {
    shutdown();
}

EntityRenderer::EntityRenderer(EntityRenderer&& other) noexcept
    : gpu_(other.gpu_),
      palette_linear_(other.palette_linear_),
      stats_(other.stats_),
      instancing_(other.instancing_),
      initialised_(other.initialised_) {
    other.gpu_ = Gpu{};
    other.initialised_ = false;
}

EntityRenderer& EntityRenderer::operator=(EntityRenderer&& other) noexcept {
    if (this == &other) return *this;
    shutdown();
    gpu_ = other.gpu_;
    palette_linear_ = other.palette_linear_;
    stats_ = other.stats_;
    instancing_ = other.instancing_;
    initialised_ = other.initialised_;
    other.gpu_ = Gpu{};
    other.initialised_ = false;
    return *this;
}

bool EntityRenderer::initialise() {
    if (initialised_) return true;

    // Armed before the first handle exists so that any failure below can hand
    // cleanup to shutdown() rather than unwind by hand. A half-constructed
    // renderer that returns false and keeps its buffers is the exact shape of
    // leak this engine keeps finding.
    initialised_ = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    gpu_.program = bgfx::createProgram(bgfx::createEmbeddedShader(kEmbeddedShaders, type, "vs_entity"),
                                       bgfx::createEmbeddedShader(kEmbeddedShaders, type, "fs_entity"),
                                       true);
    if (!bgfx::isValid(gpu_.program)) {
        log::error("failed to create the entity shader program");
        shutdown();
        return false;
    }

    // Position only, and float. Four floats rather than a tighter packing
    // because this buffer is 384 bytes total and exists for the whole run —
    // there is nothing to win by packing it, and an unnormalised integer
    // attribute here would reproduce the WebGL2 failure chunk_mesh.cpp
    // documents.
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float).end();

    gpu_.cube_vertices = bgfx::createVertexBuffer(
        bgfx::copy(kCubeVertices.data(), static_cast<u32>(kCubeVertices.size() * sizeof(CubeVertex))),
        layout);
    gpu_.cube_indices = bgfx::createIndexBuffer(
        bgfx::copy(kCubeIndices.data(), static_cast<u32>(kCubeIndices.size() * sizeof(u16))));

    if (!bgfx::isValid(gpu_.cube_vertices) || !bgfx::isValid(gpu_.cube_indices)) {
        log::error("failed to create the entity cube buffers");
        shutdown();
        return false;
    }

    // Uniform names are global in bgfx: createUniform with a name the chunk
    // pass already registered hands back a reference to the *same* uniform and
    // bumps its refcount. That is intended here — one name, one meaning across
    // both passes — and it is also why shutdown() must destroy every one of
    // them even though Renderer created them first.
    gpu_.u_entity_instance = bgfx::createUniform("u_entityInstance", bgfx::UniformType::Vec4, 4);
    gpu_.u_light_dir = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    gpu_.u_fog_params = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    gpu_.u_fog_color = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
    gpu_.u_eye_pos = bgfx::createUniform("u_eyePos", bgfx::UniformType::Vec4);
    gpu_.u_ambient_sky = bgfx::createUniform("u_ambientSky", bgfx::UniformType::Vec4);
    gpu_.u_ambient_ground = bgfx::createUniform("u_ambientGround", bgfx::UniformType::Vec4);
    gpu_.u_key_color = bgfx::createUniform("u_keyColor", bgfx::UniformType::Vec4);
    gpu_.u_rim_color = bgfx::createUniform("u_rimColor", bgfx::UniformType::Vec4);

    for (usize i = 0; i < kPalette.size(); ++i) {
        const PaletteEntry& entry = kPalette[i];
        PaletteSlot& slot = palette_linear_[i];
        for (usize c = 0; c < 3; ++c) {
            slot.chassis[c] = srgb_to_linear(entry.chassis[c]);
            // Intensity folded in here rather than carried as a fifth float per
            // instance: it is a per-palette constant, and multiplying it 220
            // times a frame on the CPU to save 4 bytes per instance would be
            // the wrong trade in both directions.
            slot.optic[c] = srgb_to_linear(entry.optic[c]) * entry.optic_intensity;
        }
    }

    // Read once, at init. bgfx caps do not change for the life of the context,
    // and the fallback needs to be a decision the draw loop does not re-make
    // 220 times a frame.
    instancing_ = (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING) != 0;
    if (!instancing_) {
        log::warn("instancing unavailable on {}; entities fall back to one draw call each",
                  bgfx::getRendererName(type));
    }

    return true;
}

void EntityRenderer::shutdown() {
    if (!initialised_) return;

    const auto destroy_uniform = [](bgfx::UniformHandle& handle) {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    };
    destroy_uniform(gpu_.u_entity_instance);
    destroy_uniform(gpu_.u_light_dir);
    destroy_uniform(gpu_.u_fog_params);
    destroy_uniform(gpu_.u_fog_color);
    destroy_uniform(gpu_.u_eye_pos);
    destroy_uniform(gpu_.u_ambient_sky);
    destroy_uniform(gpu_.u_ambient_ground);
    destroy_uniform(gpu_.u_key_color);
    destroy_uniform(gpu_.u_rim_color);

    if (bgfx::isValid(gpu_.cube_indices)) bgfx::destroy(gpu_.cube_indices);
    gpu_.cube_indices = BGFX_INVALID_HANDLE;

    if (bgfx::isValid(gpu_.cube_vertices)) bgfx::destroy(gpu_.cube_vertices);
    gpu_.cube_vertices = BGFX_INVALID_HANDLE;

    if (bgfx::isValid(gpu_.program)) bgfx::destroy(gpu_.program);
    gpu_.program = BGFX_INVALID_HANDLE;

    initialised_ = false;
}

void EntityRenderer::write_instance(std::array<f32, 16>& out, const EntityInstance& entity) const {
    // Clamp rather than assert: a bad palette index arriving from the
    // integration layer should paint an entity debug-grey, not walk off the end
    // of the table in a release build during a wave.
    const usize slot_index = std::min(static_cast<usize>(entity.palette_index), kPaletteSize - 1);
    const PaletteSlot& slot = palette_linear_[slot_index];

    // sin/cos once per entity here rather than once per vertex in the shader:
    // 24 vertices per box means the GPU would otherwise pay for it 24 times.
    const f32 cos_yaw = std::cos(entity.yaw);
    const f32 sin_yaw = std::sin(entity.yaw);
    const f32 flash = std::clamp(entity.damage_flash, 0.0f, 1.0f);

    out[0] = entity.position.x;
    out[1] = entity.position.y;
    out[2] = entity.position.z;
    out[3] = cos_yaw;

    out[4] = entity.half_extents.x;
    out[5] = entity.half_extents.y;
    out[6] = entity.half_extents.z;
    out[7] = sin_yaw;

    out[8] = slot.chassis[0];
    out[9] = slot.chassis[1];
    out[10] = slot.chassis[2];
    out[11] = flash;

    out[12] = slot.optic[0];
    out[13] = slot.optic[1];
    out[14] = slot.optic[2];

    // The fallback selector. Zero in the instance stream, one in the uniform
    // mirror; the vertex shader picks its source from this single float. Both
    // paths therefore share one layout and one unpacking path, which is the
    // only way the rarely-exercised fallback stays correct.
    out[15] = 0.0f;
}

void EntityRenderer::set_frame_uniforms(const Camera& camera) const {
    const vec3 eye = camera.eye();
    const f32 eye_pos[4] = {eye.x, eye.y, eye.z, 0.0f};
    const f32 fog_color[4] = {kFogColor[0], kFogColor[1], kFogColor[2], 1.0f};

    bgfx::setUniform(gpu_.u_light_dir, kLightDir.data());
    bgfx::setUniform(gpu_.u_fog_params, kFogParams.data());
    bgfx::setUniform(gpu_.u_fog_color, fog_color);
    bgfx::setUniform(gpu_.u_eye_pos, eye_pos);
    bgfx::setUniform(gpu_.u_ambient_sky, kAmbientSky.data());
    bgfx::setUniform(gpu_.u_ambient_ground, kAmbientGround.data());
    bgfx::setUniform(gpu_.u_key_color, kKeyColor.data());
    bgfx::setUniform(gpu_.u_rim_color, kRimColor.data());
}

void EntityRenderer::draw(bgfx::ViewId view,
                          const Camera& camera,
                          std::span<const EntityInstance> instances) {
    stats_ = EntityRendererStats{};
    if (!initialised_ || instances.empty()) return;

    // Set here rather than assumed: the entity pass has to work on any view the
    // caller hands it, not only on the one Renderer::render() happens to have
    // configured. Re-setting a view's transform with the same matrices is free.
    // The view *rect* is deliberately not touched — the owner of the view sizes
    // it, and guessing a size here would fight Renderer::resize().
    const glm::mat4 view_matrix = camera.view();
    const glm::mat4 proj = camera.projection();
    bgfx::setViewTransform(view, &view_matrix[0][0], &proj[0][0]);

    std::array<f32, 16> packed{};

    if (instancing_) {
        usize offset = 0;
        while (offset < instances.size()) {
            const u32 want = static_cast<u32>(instances.size() - offset);

            // The transient instance buffer is shared with everything else
            // drawing this frame, so ask how much of it is left instead of
            // assuming. A short answer means a smaller batch and one more draw
            // call, not a corrupted one.
            const u32 fits = bgfx::getAvailInstanceDataBuffer(want, kInstanceStride);
            if (fits == 0) {
                stats_.entities_dropped = want;
                break;
            }

            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer(&idb, fits, kInstanceStride);

            for (u32 i = 0; i < fits; ++i) {
                write_instance(packed, instances[offset + i]);
                // memcpy rather than a reinterpret_cast to f32*: idb.data is a
                // uint8_t*, and casting it up-alignment is exactly what
                // -Wcast-align exists to catch.
                std::memcpy(
                    idb.data + static_cast<usize>(i) * kInstanceStride, packed.data(), sizeof(packed));
            }

            set_frame_uniforms(camera);
            // Zeroed, so u_entityInstance[3].w is 0 and the shader reads the
            // instance stream. bgfx consumes uniform values at submit, so this
            // has to be re-set for every batch.
            const std::array<f32, 16> no_mirror{};
            bgfx::setUniform(gpu_.u_entity_instance, no_mirror.data(), 4);

            bgfx::setVertexBuffer(0, gpu_.cube_vertices);
            bgfx::setIndexBuffer(gpu_.cube_indices);
            bgfx::setInstanceDataBuffer(&idb);
            bgfx::setState(kDrawState);
            bgfx::submit(view, gpu_.program);

            ++stats_.draw_calls;
            stats_.entities_drawn += fits;
            stats_.triangles += fits * 12;
            offset += fits;
        }
        return;
    }

    // --- fallback: one draw per entity -------------------------------------
    // Only reachable on a backend without BGFX_CAPS_INSTANCING, which in
    // practice means a GLES2-class context. The shader reads the same 4 vec4s
    // from a uniform instead of from the vertex stream, selected by the 1.0
    // written into the last float below.
    for (const EntityInstance& entity : instances) {
        write_instance(packed, entity);
        packed[15] = 1.0f;

        set_frame_uniforms(camera);
        bgfx::setUniform(gpu_.u_entity_instance, packed.data(), 4);

        bgfx::setVertexBuffer(0, gpu_.cube_vertices);
        bgfx::setIndexBuffer(gpu_.cube_indices);
        bgfx::setState(kDrawState);
        bgfx::submit(view, gpu_.program);

        ++stats_.draw_calls;
        ++stats_.entities_drawn;
        stats_.triangles += 12;
    }
}

}  // namespace df
