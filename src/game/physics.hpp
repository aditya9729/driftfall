// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "voxel/voxel_world.hpp"

namespace df {

// ---------------------------------------------------------------------------
// Conventions
//
// All lengths are in voxels; 1 voxel = 0.5 m. A face lying exactly on a voxel
// plane does *not* overlap the cell beyond it — touching a wall is not being
// inside it. Every boundary decision below follows from that one rule, which
// is what makes a box resting on the deck stable instead of alternating
// between "clear" and "penetrating" on successive steps.
// ---------------------------------------------------------------------------

/// Gap left between a resolved face and the voxel plane that stopped it.
/// Resting exactly on the plane would be tidier, but `centre = plane - half`
/// is not exactly representable, so the recomposed face lands one ulp past the
/// plane about half the time and the next step reports a penetration that is
/// not real. A fixed 0.1 mm gap is invisible and makes resting bit-stable.
inline constexpr f32 kContactSkin = 1.0e-4f;

/// How far below the box we look for a floor when reporting `grounded`.
/// Must exceed kContactSkin, or a box resting on its own skin reads as
/// airborne on the step after it lands.
inline constexpr f32 kGroundProbe = 1.0e-3f;

/// Largest accepted half-extent, in voxels. This is a character primitive and
/// the overlap scan is cubic in box size, so a level-sized box is refused
/// rather than silently costing a millisecond inside a 220-enemy loop.
inline constexpr f32 kMaxHalfExtent = 8.0f;

/// Pass as `max_eject` to resolve_penetration when the push should be
/// instantaneous — spawning and teleporting, where there is no previous frame
/// for the player to have seen and a gradual shove has nothing to read as.
inline constexpr f32 kInstantEject = 1.0e9f;

/// How far outside the sector a box may drift before its position is pinned.
/// Not a wall: it is the numeric backstop that keeps an unhandled fall from
/// grinding f32 precision away over a long run. Gameplay is expected to have
/// acted on `SweepResult::out_of_bounds` long before anything gets here.
inline constexpr f32 kGuardMargin = 256.0f;

/// The one-voxel-per-axis-per-step invariant that lets resolution be discrete
/// rather than continuous. At 60 Hz, kMoveSpeed = 11 voxels/s gives 0.183
/// voxels per step; anything under 60 voxels/s stays below 1.0.
inline constexpr f32 kMaxAxisDisplacement = 1.0f;

// ---------------------------------------------------------------------------
// De-penetration
// ---------------------------------------------------------------------------

struct DepenetrationResult {
    /// Position after ejection. Equal to the input when the box was already
    /// clear, and also when it is sealed — see `sealed`.
    vec3 position{0.0f};

    /// The box overlapped solid voxels on entry. A statement about the world
    /// (a Warden rebuilt a wall through you, a Breacher took the deck out from
    /// under you, you built into your own feet), not about our success.
    bool penetrating = false;

    /// Overlapping, and no direction clears the geometry. We deliberately do
    /// not move: teleporting to the nearest free cell reads as a bug and can
    /// deposit the player inside a sealed room. Being walled in is a designed,
    /// survivable threat you shoot your way out of, so this is the caller's
    /// cue to apply crush damage and let the player breach.
    bool sealed = false;

    /// The box no longer overlaps anything at `position`. False while a
    /// rate-limited ejection is still part-way out, and false when sealed.
    bool resolved = false;
};

/// Pushes a box out of solid geometry it is overlapping.
///
/// Separate from move_and_slide on purpose: the geometry moves on steps where
/// the player does not, so this has to run on a zero-displacement step too.
///
/// The rule, in order:
///   1. Collect the solid cells the box overlaps. None: return unchanged.
///   2. For each of the six directions, compute the push that clears *every*
///      overlapped cell, not merely the nearest one.
///   3. Prefer +Y whenever that push is <= 2 * half_extents.y and the
///      destination is clear. A block placed at your feet should stand you on
///      top of it — the intuition every voxel game trains — and up is the only
///      direction that can never shove you through a wall into a sealed room.
///   4. Otherwise take the smallest of the remaining five pushes whose
///      destination is clear.
///   5. Nothing clears: do not move, and report `sealed`.
///
/// A consequence worth stating plainly: a body inside a solid mass thicker
/// than itself in every direction is `sealed`, because no single-axis push
/// clears it. That is the Warden encounter working as designed, not a gap —
/// a rebuilt wall is one to three voxels thick and ejects normally, while
/// being entombed in a solid block is meant to cost you.
///
/// `max_eject` caps the distance moved this call, in voxels, so a deep burial
/// reads as being shoved rather than as a jump cut. Pass kInstantEject on
/// spawn. A non-positive cap reports the state without moving; that is a
/// legitimate way to query penetration without acting on it.
[[nodiscard]] DepenetrationResult resolve_penetration(const VoxelWorld& world,
                                                      vec3 position,
                                                      vec3 half_extents,
                                                      f32 max_eject);

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

struct SweepResult {
    vec3 position{0.0f};

    /// Velocity with every blocked axis zeroed. Returned rather than left for
    /// the caller to re-derive from the hit flags: two copies of that rule is
    /// how gravity keeps accumulating while you stand on the deck, and the
    /// first step off a ledge drops you at terminal velocity.
    vec3 velocity{0.0f};

    bool hit_x = false;
    bool hit_y = false;
    bool hit_z = false;

    /// Solid geometry within kGroundProbe below the resolved box. A geometric
    /// query, not a jump state — coyote time belongs to the caller.
    bool grounded = false;

    /// The box centre left the sector. Not an error and not a contact: the
    /// boundary is open vacuum, exactly as VoxelWorld::at() and
    /// raycast_voxels() already treat it, and inventing a wall here would
    /// contradict both. The caller owns the policy (kill, respawn, tether);
    /// see kGuardMargin for the numeric backstop that is *not* that policy.
    bool out_of_bounds = false;
};

/// Moves an axis-aligned box by `displacement`, sliding along contacts.
///
/// `displacement` is the already-integrated move for this step and `velocity`
/// is the state to resolve against it; keeping them separate leaves the
/// integration order (gravity before or after the sweep) visible in Sim rather
/// than buried here.
///
/// Resolution is per-axis, X then Z then Y, so a blocked X leaves Z free and
/// the box slides along a wall instead of stopping dead in front of it.
/// Vertical last so grounding is decided after horizontal motion — you walk
/// off the ledge and *then* fall, rather than falling and then discovering you
/// had already left. The cost of any fixed order is that a diagonal move which
/// would just barely round a convex corner resolves X first and may catch; it
/// is deterministic, which is what a replay needs, and unnoticeable at
/// character speeds.
///
/// Per-axis |displacement| must be < kMaxAxisDisplacement, which is what makes
/// discrete resolution sound: the leading face crosses at most one voxel plane
/// per axis, so there is nothing to tunnel through. Debug builds assert it. If
/// it is violated in a release build the resolution stays conservative rather
/// than becoming wrong — the per-axis walk visits every plane the face crosses
/// and stops at the first solid one — but the flags then describe a swept move
/// the caller did not ask for, so fix the caller.
///
/// This does not de-penetrate; call resolve_penetration first. It does behave
/// sanely when the box starts overlapping, because only planes the box has
/// *not* already entered can block it. The exact invariant is narrower than
/// "cannot move deeper": a penetrating box may slide within the voxels it
/// already occupies — those cannot block it without welding it in place — but
/// it can never cross into one it was not already in. That bound is why
/// de-penetration only ever has to undo one voxel of overlap.
///
/// Degenerate input is refused rather than resolved. A non-finite position, or
/// a half-extent that is non-positive, non-finite, or above kMaxHalfExtent,
/// returns the inputs unchanged; overwriting a corrupt position with zero
/// would teleport the entity to the origin and hide the bug that made it. A
/// non-finite displacement or velocity keeps the position and zeroes the
/// velocity, because that state is recoverable.
[[nodiscard]] SweepResult move_and_slide(
    const VoxelWorld& world, vec3 position, vec3 half_extents, vec3 displacement, vec3 velocity);

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

/// True when the box overlaps at least one solid voxel. Also the check
/// Sim::place_voxel wants before letting a player build into a body.
[[nodiscard]] bool box_overlaps_solid(const VoxelWorld& world, vec3 centre, vec3 half_extents);

/// True when solid geometry sits within kGroundProbe below the box.
[[nodiscard]] bool box_grounded(const VoxelWorld& world, vec3 centre, vec3 half_extents);

}  // namespace df
