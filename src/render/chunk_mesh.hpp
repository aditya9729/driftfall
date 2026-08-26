// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "voxel/greedy_mesher.hpp"

#include <bgfx/bgfx.h>

namespace df {

/// The GPU side of one chunk's geometry.
///
/// Owns its bgfx handles and releases them on destruction, because a voxel
/// game leaks buffers the moment ownership is ambiguous: chunks are rebuilt
/// constantly and every rebuild is an allocation and a free.
class GpuChunkMesh {
public:
    GpuChunkMesh() = default;
    ~GpuChunkMesh();

    GpuChunkMesh(const GpuChunkMesh&) = delete;
    GpuChunkMesh& operator=(const GpuChunkMesh&) = delete;
    GpuChunkMesh(GpuChunkMesh&& other) noexcept;
    GpuChunkMesh& operator=(GpuChunkMesh&& other) noexcept;

    /// Replaces the contents. Passing an empty mesh releases the buffers,
    /// which is the common case when the player blows a chunk hollow.
    void upload(const ChunkMesh& mesh);

    void release();

    [[nodiscard]] bool valid() const { return bgfx::isValid(vertex_buffer_); }

    [[nodiscard]] u32 index_count() const { return index_count_; }

    [[nodiscard]] bgfx::VertexBufferHandle vertex_buffer() const { return vertex_buffer_; }

    [[nodiscard]] bgfx::IndexBufferHandle index_buffer() const { return index_buffer_; }

    /// Must be called once before any upload().
    static void init_layout();

    static const bgfx::VertexLayout& layout() { return s_layout; }

private:
    bgfx::VertexBufferHandle vertex_buffer_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle index_buffer_ = BGFX_INVALID_HANDLE;
    u32 index_count_ = 0;

    static bgfx::VertexLayout s_layout;
};

}  // namespace df
