// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "voxel/voxel_world.hpp"

namespace df {

// ---------------------------------------------------------------------------
// Build-mode targeting.
//
// This layer answers one question — "where would a build action land?" — and
// answers it without touching Sim, the renderer, or input. That separation is
// the whole point: the HUD needs the answer to draw a ghost cube every frame,
// Sim needs it to decide whether to charge the player, and a test needs it
// without a window. All three get the same function.
//
// Conventions match physics.hpp, deliberately: all lengths are in voxels,
// 1 voxel = 0.5 m, and a face lying exactly on a voxel plane does *not* overlap
// the cell beyond it. Two files disagreeing about what "touching" means is how
// a player ends up standing inside a wall they just built.
// ---------------------------------------------------------------------------

/// Voxel edge length in metres. The scale everything else is derived from.
inline constexpr f32 kVoxelMetres = 0.5f;

/// The player's collision box, in voxels — 0.5 m wide, 1.8 m tall.
inline constexpr vec3 kPlayerHalfExtents{0.5f, 1.8f, 0.5f};

/// How far a build action reaches, in voxels — 7 voxels, 3.5 m.
///
/// Measure it from `Camera::head()`, not `Camera::eye()`. head() is the
/// player's shoulder; eye() is up to `Camera::distance()` (5.5 voxels) further
/// back down the same axis. The two are the same *line*, so the cursor lands on
/// the same cell either way, but the number means different things: 7 from the
/// head is 3.5 m of player reach, and 7 from the eye is 1.5 voxels of player
/// reach, which is unusable. Same reason camera.hpp gives for firing from
/// head(): the eye can be inside geometry when the arm is pulled into cover.
///
/// Both ends of 7 were picked against a failure:
///
///   Too short — a standing player's head sits about 4.6 voxels up, and a
///   generated interior wall is 6 tall. Under about 4 you cannot top off a
///   wall or seal the upper corner of a 3x3 doorway without shuffling
///   backwards and forwards for individual cells. On a touchscreen, where
///   pitch comes off a floating thumb stick, that is the difference between
///   fortifying and fighting the controls.
///
///   Too long — the sector is a 24-voxel room grid. At 12 (half a room) a
///   player standing at the centre of a room could seal the far wall's
///   doorway, including the side of it they cannot see, and wall an enemy path
///   shut from across the room. Fortifying should be a statement about where
///   you are standing.
///
/// 7 also bounds cantilevering: each placement needs a solid face in reach, so
/// you can extend a lip about six voxels off a ledge and no further. Enough to
/// build cover, not enough to bridge a room from safety.
inline constexpr f32 kBuildReach = 7.0f;

/// A turret's column: 1 voxel of deck, 2 voxels tall.
inline constexpr i32 kTurretHeightVoxels = 2;

/// The turret's collision box, in voxels. Narrower than the cell it stands in
/// so it never wedges against the column walls at rest.
inline constexpr vec3 kTurretHalfExtents{0.4f, 1.0f, 0.4f};

// ---------------------------------------------------------------------------
// Voxel placement
// ---------------------------------------------------------------------------

/// Where a build action would land, given where the player is looking.
///
/// `valid` and `affordable` are separate on purpose. A cursor on a legal cell
/// that the player cannot pay for should still draw — greyed out, with the
/// price — because "you are aiming at nothing" and "you are broke" are
/// different problems and the player fixes them differently.
struct BuildCursor {
    ivec3 target{0};     ///< the voxel cell that would be filled
    ivec3 anchor{0};     ///< the solid cell it attaches to
    bool valid = false;  ///< false when there is nothing to build against
    i32 cost = 0;        ///< salvage price of `material`; 0 when unbuildable
    bool affordable = false;
};

/// Resolves the build cursor by raycasting the world.
///
/// The placement rule is the one every voxel game uses: the new voxel goes in
/// the empty cell adjacent to the face the ray entered through, i.e.
/// `hit.voxel + hit.normal`. Predictability beats cleverness here — the player
/// has to be able to guess where the block lands before they commit salvage to
/// it, and any rule that is not "against the face you are pointing at" fails
/// that test.
///
/// Refused, with `valid = false`, when: nothing solid lies within `reach`; the
/// material is not buildable; the target cell is outside the sector (the hull
/// looking outward — the sector boundary is open vacuum, and building into
/// vacuum is how you get geometry no chunk owns); the target cell is already
/// solid; or the ray began inside solid geometry, which crosses no face and so
/// names no side to build on.
///
/// Degenerate input — non-finite origin or direction, a zero direction, a
/// non-positive or non-finite reach — is refused rather than resolved, matching
/// raycast_voxels(). The origin check is ours: raycast_voxels floors the origin
/// straight into an i32, so a NaN position must be stopped before it gets
/// there.
///
/// Allocates nothing and holds no state; this runs every frame.
[[nodiscard]] BuildCursor compute_build_cursor(
    const VoxelWorld& world, vec3 origin, vec3 direction, f32 reach, Voxel material, i32 salvage);

/// True if filling `cell` would intersect an axis-aligned body at `centre`.
///
/// The cell owns the half-open box [cell, cell + 1) and the body owns
/// [centre - half, centre + half]; overlap on every axis is strict, so a body
/// standing flush against a cell's face is *not* inside it. That is the same
/// rule physics.hpp resolves contacts with, and it has to be, or a player
/// resting on the deck could not build the cell they are standing on.
///
/// Sim::place_voxel wants this before it commits: without it the player can
/// build a barricade into their own feet and end up sealed inside geometry.
/// Prevention is this one comparison; the cure is de-penetration, which is far
/// more expensive and cannot always succeed (resolve_penetration reports
/// `sealed`). Prevention does not cover everything — the Warden rebuilds walls
/// through you on purpose, and that is what the cure is for — but it covers the
/// case the player causes themselves, which is the one that reads as a bug.
///
/// A non-finite centre or extent, or a negative extent, returns true: a body we
/// cannot describe is a body we refuse to build into. The tradeoff is that this
/// query can answer "yes" about a box that does not geometrically intersect
/// anything, which is a lie in service of never trapping the player. Corrupt
/// state should block a build, not licence one.
///
/// physics.hpp's box_overlaps_solid returns *false* on the same input. That is
/// not an inconsistency to tidy away: there, false means "do not push", here
/// true means "do not build", and both are the inert answer for their caller.
[[nodiscard]] bool cell_intersects_body(ivec3 cell, vec3 centre, vec3 half_extents);

// ---------------------------------------------------------------------------
// Turret placement
// ---------------------------------------------------------------------------

/// Where a turret would stand.
struct TurretSite {
    ivec3 base{0};        ///< lowest cell of the turret's column
    ivec3 anchor{0};      ///< the solid cell it stands on
    vec3 position{0.0f};  ///< the turret's centre; hand this to Sim::deploy_turret
    bool valid = false;
    i32 cost = 0;
    bool affordable = false;
};

/// True when a turret fits with `base` as its lowest cell: every cell of the
/// kTurretHeightVoxels-tall column is inside the sector and empty, and the cell
/// directly under `base` is solid.
///
/// A turret needs both halves. Clear volume alone puts one hovering in a
/// stairwell; a floor alone lets you deploy into the voxel you are about to
/// seal. Requiring the deck cell also means the site is decided by geometry
/// that exists, not by whatever the player happens to be standing on.
[[nodiscard]] bool turret_site_clear(const VoxelWorld& world, ivec3 base);

/// Resolves a turret site by raycasting, the same way compute_build_cursor
/// resolves a voxel.
///
/// Only an upward-facing hit counts. A turret is a thing that stands, so the
/// ray has to land on a floor: `hit.normal` must be {0, +1, 0}. Pointing at a
/// wall gives `valid = false` rather than silently sliding the turret to some
/// nearby patch of deck, because a placement that moves after you commit is a
/// placement you cannot aim.
///
/// `cost` is passed in rather than looked up: the turret price lives in an
/// anonymous namespace inside sim.cpp, and duplicating a balance constant into
/// a second translation unit is how the two drift apart. Hoist it to
/// `turret_cost()` in sim.hpp and pass that.
[[nodiscard]] TurretSite compute_turret_site(
    const VoxelWorld& world, vec3 origin, vec3 direction, f32 reach, i32 cost, i32 salvage);

}  // namespace df
