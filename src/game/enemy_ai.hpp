// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/types.hpp"
#include "game/nav_field.hpp"
#include "game/sim.hpp"
#include "voxel/voxel_world.hpp"

#include <entt/entity/registry.hpp>

namespace df {

// --- result -----------------------------------------------------------------

/// What one enemy step produced that this module does not own.
///
/// Player health lives in Sim, not here, so the damage is returned rather than
/// applied. That keeps the whole horde testable without a Sim: a test can
/// stand up a bare registry and read the number back.
struct EnemyTickResult {
    f32 damage_to_player = 0.0f;
    i32 attacks_landed = 0;

    /// At least one living enemy has stopped making progress toward the player
    /// and is not deliberately holding position. See kStuckSeconds — this is
    /// the anti-soft-lock signal, and it ships even after navigation works.
    bool any_enemy_stuck = false;
};

// --- tuning -----------------------------------------------------------------

/// Everything that makes one archetype play differently from another.
///
/// One struct rather than five branches scattered through the tick: the numbers
/// that define an archetype should be readable side by side, because balancing
/// them against each other is the actual work.
///
/// Lengths are in voxels, and 1 voxel is 0.5 m. Speeds are voxels/second; the
/// player moves at 11 (see app.cpp kMoveSpeed), and only the Skitter beats it.
struct EnemyTuning {
    /// Half-extents of the collision box, in voxels.
    ///
    /// Height is capped for every kind by the sector, not by taste: doorways
    /// are punched three voxels tall (generate_test_sector), so a body taller
    /// than that cannot leave the room it spawned in no matter how good the
    /// navigation is. See kMaxDoorwayHalfHeight.
    vec3 half_extents{0.4f, 0.9f, 0.4f};

    f32 move_speed = 11.0f;

    /// Radians/second the body may rotate its facing. Deliberately decoupled
    /// from movement: the Bulwark walks where the flow field points but turns
    /// its plate slowly, which is what makes strafing around it work. Treads
    /// and a turret, not a rigid body.
    f32 turn_rate = 12.0f;

    /// Centre-to-centre distance at which a melee attack connects.
    f32 contact_range = 1.8f;

    f32 contact_damage = 5.0f;

    /// Seconds between attacks. Never per-tick: contact damage applied every
    /// fixed step is 60x the intended rate and kills a full-health player in
    /// under a second. This has been a bug in every horde game ever shipped.
    f32 attack_interval = 0.65f;

    /// Distance the kind wants to hold from the player, or 0 for the kinds
    /// that close. A Lancer that closes is a slow Skitter; the standoff is the
    /// entire archetype.
    f32 preferred_range = 0.0f;
};

[[nodiscard]] EnemyTuning tuning_for(EnemyKind kind);

/// The body NavField::rebuild must be given.
///
/// There is one field and five body sizes, so the field has to be built for
/// the largest of them: a route a Warden fits through is a route every kind
/// fits through, whereas a field built for a Skitter routes the Warden into a
/// gap it cannot enter, which is the soft-lock again wearing a hat. The cost
/// is that small kinds decline gaps only they could use — cheap, and it fails
/// in the safe direction.
///
/// Sim must pass this to rebuild(); it is a function rather than a comment in
/// the report precisely so the two cannot drift apart.
[[nodiscard]] vec3 nav_body_half_extents();

// --- scale and geometry constraints ------------------------------------------

/// Largest half-height that still fits through a generated doorway.
///
/// generate_test_sector punches openings at y = 1..3 over deck plating at
/// y = 0, so a grounded body spans world y from 1.0 upward and has three
/// voxels of headroom. Every enemy is sized under this. Note that the player's
/// declared half-extents (0.5, 1.8, 0.5) are *not* — see the report.
inline constexpr f32 kMaxDoorwayHalfHeight = 1.5f;

/// Downward acceleration, voxels/s^2. 9.81 m/s^2 at 0.5 m per voxel.
inline constexpr f32 kGravity = 19.62f;

/// Fall speed cap, voxels/s. Chosen against physics.hpp's kMaxAxisDisplacement
/// of 1.0 voxel per step: at 60 Hz this is 0.67 voxels per step, so a falling
/// body stays inside the invariant that makes discrete sweep resolution sound.
/// The tick clamps the displacement as well, because the invariant is the
/// caller's to keep and a dt we did not choose must not be able to break it.
inline constexpr f32 kTerminalFall = 40.0f;

/// How fast a body overlapping solid geometry is pushed back out, voxels/s.
/// Rate-limited rather than instant so a Warden burying an enemy reads as a
/// shove; the exception is an enemy's first step, which ejects instantly
/// because there is no previous frame for anyone to have seen.
inline constexpr f32 kEjectSpeed = 12.0f;

/// Damage per second to an enemy that is inside geometry with nowhere to go.
///
/// This is the anti-soft-lock guard's teeth. `any_enemy_stuck` tells Sim that
/// something is wrong; an entombed machine that also *breaks* means a wave
/// cannot be held open forever by one body a Warden happened to wall in. A
/// Skitter dies in under a second, a Bulwark in five, a Warden in forty — long
/// enough that sealing the boss is not a strategy.
inline constexpr f32 kSealedCrushDamagePerSecond = 45.0f;

// --- the Bulwark's armour ----------------------------------------------------

/// Damage scale on the Bulwark's front plate and on its exposed back.
///
/// The spread is large on purpose. A 10 % armour bonus is a stat; a 5x swing
/// is a mechanic, and "flank it" has to be the obvious answer to the silhouette
/// rather than a thing the wiki tells you later.
inline constexpr f32 kBulwarkFrontMultiplier = 0.35f;
inline constexpr f32 kBulwarkRearMultiplier = 1.65f;

/// Damage scale for a hit on `kind`, given where the body is facing and which
/// way the damage is travelling.
///
/// `hit_direction` is the direction the *damage* moves — from the attacker
/// toward the enemy. So a shot into the front plate travels against the
/// enemy's facing and a shot into its back travels with it. That sign is easy
/// to get backwards, which is why it is stated here and asserted in both
/// directions in the tests.
///
/// Continuous rather than a front/side/rear cone: a cone puts a cliff in the
/// middle of the arc where one degree of movement doubles the player's damage,
/// which reads as a bug rather than as a rule. Only the y component is ignored
/// (armour is a vertical plate, so shooting one from above is a flank, not a
/// hole in the model). Kinds without armour return 1, and a zero-length
/// direction returns 1 because there is nothing to measure an angle against.
[[nodiscard]] f32 armour_multiplier(EnemyKind kind, f32 facing_yaw, vec3 hit_direction);

/// Applies `raw_damage` to an enemy through its armour and returns the damage
/// actually dealt.
///
/// Every damage source aimed at an enemy must go through this — turrets, the
/// player's weapon, a Breacher catching a neighbour in its blast. Subtracting
/// from Health directly still compiles and silently skips the Bulwark's plate,
/// so the plate would work in exactly the situations nobody tested.
f32 damage_enemy(entt::registry& registry, entt::entity enemy, f32 raw_damage, vec3 hit_direction);

// --- per-kind behaviour constants --------------------------------------------

/// Distance inside which an enemy abandons the flow field and steers straight
/// at the player. Two nav cells. Gated on line of sight as well as distance,
/// so "close" can never mean "close, through a wall" — steering at a player on
/// the far side of a partition is precisely the soft-lock nav_field exists to
/// prevent.
inline constexpr f32 kDirectSteerRange = 8.0f;

/// Lancer: opens fire inside this, and gives ground inside 0.7x of it.
inline constexpr f32 kLancerRange = 24.0f;
inline constexpr f32 kLancerDamage = 8.0f;
inline constexpr f32 kLancerInterval = 1.4f;

/// Breacher: voxels destroyed within this radius of the detonation, and the
/// damage each one takes.
///
/// 12 is chosen against voxel_toughness, not by feel: it destroys Barricade
/// (6), Bulkhead (12), HullPlate (3), Ore and Ice outright, and leaves
/// Reinforced (20) standing. So the 9-salvage wall survives a Breacher and the
/// 2-salvage one does not, which is the entire reason the two materials have
/// different prices.
inline constexpr i32 kBreachVoxelRadius = 2;
inline constexpr u8 kBreachVoxelDamage = 12;

/// Breacher: blast damage at the centre, falling linearly to zero at the edge.
inline constexpr f32 kBreachPlayerDamage = 34.0f;
inline constexpr f32 kBreachPlayerRadius = 4.0f;

/// Breacher: detonates on the player inside this, or after being unable to
/// make progress against geometry for this long. The second trigger is the
/// important one — it is what turns a barricade from a solution into a delay.
inline constexpr f32 kBreachTriggerRange = 2.6f;
inline constexpr f32 kBreachBlockedFuse = 0.5f;

/// Warden: seals the ground the player is retreating onto, this far past them,
/// this often, out to this range. One pillar per build, walking sideways each
/// time, so a stationary player is walled in over several builds rather than
/// instantly.
inline constexpr f32 kWardenBuildDistance = 5.0f;
inline constexpr f32 kWardenBuildInterval = 2.5f;
inline constexpr f32 kWardenBuildRange = 40.0f;
inline constexpr i32 kWardenPillarHeight = 3;

// --- the anti-soft-lock guard -------------------------------------------------

/// How long an enemy may fail to improve its nav distance before it counts as
/// stuck.
///
/// Generous on purpose: the slowest kind covers three nav cells in this time,
/// so requiring one cell of improvement cannot fire on an enemy that is merely
/// taking a corner wide. It fires on an enemy that is pinned, unreachable, or
/// standing in geometry that appeared around it.
inline constexpr f32 kStuckSeconds = 4.0f;

/// How far ahead a body checks its own box against the world before committing
/// to the flow field's direction, over and above its own width.
///
/// The field is four voxels to a cell and it aims at cell centres, but a
/// doorway is three voxels wide inside a cell that is otherwise wall — so the
/// direction it hands back is correct about the *cell* and points into
/// masonry. This is the gap between a route and a path, and closing it is
/// local steering, not a second pathfinder: a blocked body fans its heading
/// outward in fixed steps and takes the first opening, which walks it along
/// the wall to the door.
inline constexpr f32 kLookahead = 1.5f;

/// Fan applied to a blocked heading, in radians, tried in this order.
///
/// Stops at exactly 90 degrees, and that last entry is not decoration: a body
/// resting flush against a wall still has some component into it at 85, so the
/// fan without a true perpendicular finds no opening and grinds. Nothing past
/// 90, because reversing is a routing decision and the flow field owns it;
/// two systems in charge of that choice will disagree.
inline constexpr f32 kDeflectionAngles[5] = {0.436f, 0.785f, 1.134f, 1.484f, 1.5707963f};

/// How much of the blocked heading is kept when sliding around an obstruction.
///
/// Without it a deflected body travels exactly parallel to the wall and so
/// holds station a full lookahead away from it — enemies float two voxels off
/// every surface, and a Breacher can never actually reach the barricade it
/// exists to destroy. Keeping a fraction of the original heading presses the
/// body against the wall while it slides; move_and_slide discards the
/// component that goes into the voxel, so the press costs no speed.
inline constexpr f32 kWallPress = 0.30f;

/// Shrink applied to the body when probing a candidate heading, in voxels.
///
/// A body resting against a wall has a face exactly on the voxel plane, and
/// "exactly on the plane" is not an overlap — but any probe that reaches even
/// a hair further is. Without a tolerance the body cannot prove that sliding
/// along the wall is legal, so it stands there instead. Fixed rather than
/// proportional, so a Warden is not ten times more optimistic than a Skitter.
inline constexpr f32 kProbeShrink = 0.05f;

/// Ring radii, in voxels, for the unstick probe. An enemy whose own cell has
/// no flow direction — spawned inside the hull, buried by a Warden — looks
/// outward at a fixed set of offsets for a cell that does, and walks to the
/// best one. Fixed table, fixed order, no allocation, no randomness.
inline constexpr f32 kUnstickProbeNear = static_cast<f32>(NavField::kCellSize);
inline constexpr f32 kUnstickProbeFar = static_cast<f32>(NavField::kCellSize) * 2.0f;

// --- per-enemy state ----------------------------------------------------------

/// Scratch state for one enemy, attached lazily by tick_enemies().
///
/// Lazy rather than emplaced at spawn so that Sim's spawn path needs no change
/// and a test can create a bare enemy and tick it. The cost is one
/// get_or_emplace per enemy per step, which is a pool lookup.
struct EnemyAgent {
    /// Seconds until this enemy may attack again. Counted down, never reset to
    /// a partial interval, so the attack rate does not depend on the tick rate.
    f32 attack_cooldown = 0.0f;

    /// Best (smallest) nav distance this enemy has ever reached, or -1 before
    /// it has ever been on the field. Monotone by construction: progress means
    /// beating your own record, which cannot be faked by pacing back and forth
    /// across a cell boundary.
    i32 best_distance = -1;

    f32 no_progress_seconds = 0.0f;

    /// Seconds spent in contact with geometry while going nowhere. Breacher
    /// fuse; unused by the other kinds.
    f32 blocked_seconds = 0.0f;

    /// The way round an obstruction this body has committed to, in world
    /// space, or zero when the flow field's own direction is clear.
    ///
    /// World space and not "left or right of my heading", which is the version
    /// that does not work: the heading rotates as the body slides, so a
    /// heading-relative side silently reverses, and the body oscillates about
    /// the point where the two definitions cross. Storing the direction itself
    /// means a committed body keeps going the same way down the wall.
    vec3 wall_tangent{0.0f};

    /// Warden only: how many pillars it has raised, which is also the lateral
    /// offset of the next one.
    i32 build_index = 0;

    f32 build_cooldown = kWardenBuildInterval;

    /// Cleared once the enemy has been put somewhere legal. Sim still spawns
    /// the whole wave at the sector origin, which is inside the hull corner
    /// (see the TODO in Sim::spawn_pending_enemies), and a body that begins
    /// buried in the deck ejects sideways out of the sector. Until spawning
    /// moves to breach points, the first step relocates such an enemy onto the
    /// deck; once it does, this becomes a no-op rather than dead code that
    /// disagrees with the spawner.
    bool placed = false;

    /// Mirrors the last stuck verdict for the debug HUD. Not read by the tick.
    bool stuck = false;
};

// --- the tick -----------------------------------------------------------------

/// Advances every enemy one fixed step. Returns what the caller must apply to
/// state this function does not own.
///
/// `world` is non-const because the Breacher destroys voxels and the Warden
/// places them. `player_position` is the player's box centre, in voxels.
///
/// Enemies do not collide with each other. That is a deliberate omission: 220
/// bodies with mutual separation is an O(n^2) pass and a tuning problem, and
/// its absence has one visible consequence — a horde overlaps in a doorway
/// instead of queueing. It also means no enemy can ever be stuck behind
/// another, which keeps the stuck signal below about geometry.
[[nodiscard]] EnemyTickResult tick_enemies(
    entt::registry& registry, VoxelWorld& world, const NavField& nav, vec3 player_position, f32 dt);

}  // namespace df
