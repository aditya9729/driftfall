// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0

#include "render/chunk_mesh.hpp"

#include <utility>

namespace df {

bgfx::VertexLayout GpuChunkMesh::s_layout;

void GpuChunkMesh::init_layout() {
    // Mirrors PackedVertex exactly. Both attributes are *normalised*, which is
    // not a stylistic choice. bgfx's GL path does this:
    //
    //     if ((OpenGL >= 30 || gles3) && !isFloat(type) && !normalized)
    //         glVertexAttribIPointer(...);
    //
    // so an unnormalised Uint8 attribute is bound as an *integer* attribute on
    // GLES3 and WebGL2 — exactly the backends this game ships on. The shader
    // declares a_position as vec4, the types disagree, and every draw is
    // rejected with GL_INVALID_OPERATION. Nothing is reported by bgfx; the
    // draw-call count still looks perfectly healthy and the screen stays
    // empty. Normalising keeps it a float attribute everywhere, at the cost of
    // one multiply by 255 in the vertex shader.
    s_layout.begin()
        .add(bgfx::Attrib::Position, 4, bgfx::AttribType::Uint8, true, false)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, false)
        .end();
    static_assert(sizeof(PackedVertex) == 8, "vertex layout and PackedVertex must agree");
}

GpuChunkMesh::~GpuChunkMesh() {
    release();
}

GpuChunkMesh::GpuChunkMesh(GpuChunkMesh&& other) noexcept
    : vertex_buffer_(other.vertex_buffer_),
      index_buffer_(other.index_buffer_),
      index_count_(other.index_count_) {
    other.vertex_buffer_ = BGFX_INVALID_HANDLE;
    other.index_buffer_ = BGFX_INVALID_HANDLE;
    other.index_count_ = 0;
}

GpuChunkMesh& GpuChunkMesh::operator=(GpuChunkMesh&& other) noexcept {
    if (this == &other) return *this;
    release();
    vertex_buffer_ = other.vertex_buffer_;
    index_buffer_ = other.index_buffer_;
    index_count_ = other.index_count_;
    other.vertex_buffer_ = BGFX_INVALID_HANDLE;
    other.index_buffer_ = BGFX_INVALID_HANDLE;
    other.index_count_ = 0;
    return *this;
}

void GpuChunkMesh::release() {
    if (bgfx::isValid(vertex_buffer_)) bgfx::destroy(vertex_buffer_);
    if (bgfx::isValid(index_buffer_)) bgfx::destroy(index_buffer_);
    vertex_buffer_ = BGFX_INVALID_HANDLE;
    index_buffer_ = BGFX_INVALID_HANDLE;
    index_count_ = 0;
}

void GpuChunkMesh::upload(const ChunkMesh& mesh) {
    release();
    if (mesh.empty()) return;

    // bgfx::copy, not makeRef: the CPU-side mesh was produced on a worker
    // thread and is about to go out of scope. makeRef here is a use-after-free
    // that only shows up on some drivers, which is the worst kind.
    const bgfx::Memory* vertices =
        bgfx::copy(mesh.vertices.data(), static_cast<u32>(mesh.vertices.size() * sizeof(PackedVertex)));
    const bgfx::Memory* indices =
        bgfx::copy(mesh.indices.data(), static_cast<u32>(mesh.indices.size() * sizeof(u32)));

    vertex_buffer_ = bgfx::createVertexBuffer(vertices, s_layout);
    index_buffer_ = bgfx::createIndexBuffer(indices, BGFX_BUFFER_INDEX32);
    index_count_ = static_cast<u32>(mesh.indices.size());
}

}  // namespace df
