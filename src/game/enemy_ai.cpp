// Copyright 2026 Aditya Gudal
// SPDX-License-Identifier: Apache-2.0
//
// The horde.
//
// Three rules run through every archetype here:
//
//  1. Navigation is the NavField, never a vector at the player. The sector is
//     partitioned by full-height walls with punched doorways, so steer-at-the-
//     target pins the whole wave on the first partition and the Assault phase
//     never ends. Direct steering is allowed only inside kDirectSteerRange and
//     only with line of sight, which together mean "already in the room".
//  2. Damage to the player is on an interval, never per step. At 60 Hz a
//     per-step melee is 60x its intended rate.
//  3. Nothing here allocates or branches on iteration order. Up to 220 bodies
//     run this every fixed step, and the replay system needs two runs of the
//     same seed to produce the same bytes.

#include "game/enemy_ai.hpp"

#include "game/physics.hpp"
#include "voxel/raycast.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace df {

namespace {

constexpr f32 kEpsilon = 1.0e-4f;

/// Half a diagonal step, precomputed. The unstick probe runs eight directions
/// per ring and there is no reason to pay a sqrt for a constant.
constexpr f32 kDiag = 0.70710678f;

[[nodiscard]] vec3 flatten(vec3 v) {
    return {v.x, 0.0f, v.z};
}

/// Unit vector in the XZ plane, or zero when there is no direction to take.
/// Callers must handle the zero case; normalising it would produce NaN and the
/// NaN would then propagate through a position and out into the render.
[[nodiscard]] vec3 flat_unit(vec3 v) {
    const vec3 flat = flatten(v);
    const f32 length_sq = glm::dot(flat, flat);
    if (length_sq < kEpsilon * kEpsilon) return vec3{0.0f};
    return flat / std::sqrt(length_sq);
}

/// Matches Camera::flat_forward — yaw 0 faces +Z. Sharing the convention with
/// the camera is what lets an enemy's facing be drawn without a second rule.
[[nodiscard]] vec3 yaw_forward(f32 yaw) {
    return {std::sin(yaw), 0.0f, std::cos(yaw)};
}

[[nodiscard]] f32 wrap_angle(f32 radians) {
    constexpr f32 kPi = 3.14159265f;
    constexpr f32 kTwoPi = 2.0f * kPi;
    f32 wrapped = std::fmod(radians + kPi, kTwoPi);
    if (wrapped < 0.0f) wrapped += kTwoPi;
    return wrapped - kPi;
}

/// Rotates `yaw` toward `heading` by at most turn_rate * dt.
[[nodiscard]] f32 slew_yaw(f32 yaw, vec3 heading, f32 turn_rate, f32 dt) {
    if (glm::dot(heading, heading) < kEpsilon) return yaw;
    const f32 desired = std::atan2(heading.x, heading.z);
    const f32 delta = wrap_angle(desired - yaw);
    const f32 step = turn_rate * dt;
    return wrap_angle(yaw + std::clamp(delta, -step, step));
}

/// True when nothing solid sits between the two points.
///
/// raycast_voxels stops at the first solid voxel, so a hit closer than the
/// target blocks and a hit at or past it does not. A ray that starts inside
/// solid geometry reports a hit at distance zero, which correctly reads as
/// "no line of sight" — an enemy buried in a wall cannot see out of it.
[[nodiscard]] bool has_line_of_sight(const VoxelWorld& world, vec3 from, vec3 to) {
    const vec3 delta = to - from;
    const f32 length = glm::length(delta);
    if (length < kEpsilon) return true;
    const VoxelHit hit = raycast_voxels(world, from, delta / length, length);
    return !hit.hit || hit.distance >= length;
}

/// Where an enemy's eye and muzzle sit, relative to its box centre. Slightly
/// above the middle rather than at the top, so a body that is exactly as tall
/// as a doorway does not lose sight of the player through it.
[[nodiscard]] vec3 eye_of(vec3 position, const EnemyTuning& tuning) {
    return position + vec3{0.0f, tuning.half_extents.y * 0.5f, 0.0f};
}

[[nodiscard]] vec3 rotate_y(vec3 v, f32 radians) {
    const f32 s = std::sin(radians);
    const f32 c = std::cos(radians);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

/// Whether this body could occupy the ground a step ahead along `heading`.
///
/// Two samples rather than one: the midpoint catches a wall the far sample
/// would straddle. Not a sweep — move_and_slide already resolves the actual
/// motion, and this only has to be good enough to reject a heading that ends
/// inside masonry.
[[nodiscard]] bool heading_is_clear(
    const VoxelWorld& world, vec3 position, vec3 half_extents, vec3 heading, f32 reach) {
    const vec3 probe = glm::max(half_extents - vec3{kProbeShrink}, vec3{kEpsilon});
    return !box_overlaps_solid(world, position + heading * (reach * 0.5f), probe) &&
           !box_overlaps_solid(world, position + heading * reach, probe);
}

/// Bends a blocked heading around local geometry, committing to one side.
///
/// The commitment is the whole trick. Re-choosing a side every step in front
/// of a wall makes a body oscillate in place; committing until the flow
/// field's own direction comes clear again makes it follow the wall to the
/// opening. The side is picked once, from the field rather than from a coin:
/// whichever flank is nearer the goal in cell terms.
[[nodiscard]] vec3 avoid_locally(const VoxelWorld& world,
                                 const NavField& nav,
                                 vec3 position,
                                 vec3 half_extents,
                                 vec3 heading,
                                 EnemyAgent& agent) {
    const f32 reach = kLookahead + half_extents.x + half_extents.z;

    // The route is open again: drop the commitment and take it. This, and not
    // "the nav distance improved", is the right release condition. Walking
    // along a wall toward the goal improves the distance every few steps, so
    // releasing on that turns the body back into the wall over and over and it
    // never gets far enough along to find the opening.
    if (heading_is_clear(world, position, half_extents, heading, reach)) {
        agent.wall_tangent = vec3{0.0f};
        return heading;
    }

    // Keeping a little of the blocked heading is what keeps the body against
    // the wall rather than orbiting it at arm's length.
    const auto press = [heading](vec3 tangent) { return flat_unit(tangent + heading * kWallPress); };

    // Still going round, and the way we chose is still open. Take it without
    // re-deciding, because re-deciding is the oscillation.
    if (glm::dot(agent.wall_tangent, agent.wall_tangent) > kEpsilon &&
        heading_is_clear(world, position, half_extents, agent.wall_tangent, reach)) {
        return press(agent.wall_tangent);
    }

    // Pick a way round: every deflection that is actually clear, scored by how
    // close it gets to the goal in nav terms. Scoring by the field rather than
    // by the angle is what sends a body down the side of the wall the doorway
    // is on instead of the side it happens to be facing.
    constexpr f32 kQuarterTurn = 1.5707963f;
    const f32 probe_radius = static_cast<f32>(NavField::kCellSize);
    const i32 left = nav.distance_at(position + rotate_y(heading, kQuarterTurn) * probe_radius);
    const i32 right = nav.distance_at(position + rotate_y(heading, -kQuarterTurn) * probe_radius);
    // Unreachable loses to anything; an exact tie goes left, because the tie
    // has to break the same way in both of two identical runs.
    const f32 first_side = (right >= 0 && (left < 0 || right < left)) ? -1.0f : 1.0f;

    vec3 best{0.0f};
    i32 best_distance = -1;
    for (i32 attempt = 0; attempt < 2; ++attempt) {
        const f32 side = attempt == 0 ? first_side : -first_side;
        for (const f32 angle : kDeflectionAngles) {
            const vec3 candidate = rotate_y(heading, angle * side);
            if (!heading_is_clear(world, position, half_extents, candidate, reach)) continue;
            const i32 distance = nav.distance_at(position + candidate * probe_radius);
            const bool better = best_distance < 0 ? true : (distance >= 0 && distance < best_distance);
            if (glm::dot(best, best) < kEpsilon || better) {
                best = candidate;
                best_distance = distance;
            }
        }
    }

    if (glm::dot(best, best) < kEpsilon) {
        // Boxed in on every side. Keep the field's direction and let
        // move_and_slide grind along the contact: the stuck rule is watching,
        // and inventing a direction here would only hide that from it.
        return heading;
    }

    agent.wall_tangent = best;
    return press(best);
}

/// A heading out of a cell the flow field cannot answer for.
///
/// Enemies spawn at the sector origin, which is inside the hull, and a Warden
/// can bury one mid-run. Both cases produce a zero direction from the field,
/// and doing nothing about it is a soft-lock with extra steps. So look outward
/// at a fixed table of offsets and walk toward the best cell that has an
/// answer. Fixed table, fixed order, strict improvement to break ties: two
/// identical sims pick the same probe.
[[nodiscard]] vec3 unstick_heading(const NavField& nav, vec3 position) {
    static constexpr vec3 kProbes[8] = {
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
        {kDiag, 0.0f, kDiag},
        {kDiag, 0.0f, -kDiag},
        {-kDiag, 0.0f, kDiag},
        {-kDiag, 0.0f, -kDiag},
    };
    static constexpr f32 kRadii[2] = {kUnstickProbeNear, kUnstickProbeFar};

    i32 best_distance = -1;
    vec3 best_heading{0.0f};
    for (const f32 radius : kRadii) {
        for (const vec3& probe : kProbes) {
            const i32 distance = nav.distance_at(position + probe * radius);
            if (distance < 0) continue;
            if (best_distance < 0 || distance < best_distance) {
                best_distance = distance;
                best_heading = probe;
            }
        }
        // The near ring wins outright when it has any answer at all: walking to
        // an adjacent open cell is a smaller commitment than crossing two.
        if (best_distance >= 0) break;
    }
    return best_heading;
}

/// This step's move, clamped to the invariant move_and_slide is built on.
///
/// physics.hpp requires each axis of the displacement to stay under one voxel,
/// which is what makes discrete per-axis resolution sound — the leading face
/// crosses at most one plane, so there is nothing to tunnel through. Every
/// speed here is well inside that at 60 Hz; the clamp exists because dt is an
/// argument, not a constant, and a caller's bad step must degrade into a short
/// move rather than into a body passing through a wall.
[[nodiscard]] vec3 step_displacement(vec3 velocity, f32 dt) {
    constexpr f32 kLimit = kMaxAxisDisplacement * 0.98f;
    const vec3 raw = velocity * dt;
    return {std::clamp(raw.x, -kLimit, kLimit),
            std::clamp(raw.y, -kLimit, kLimit),
            std::clamp(raw.z, -kLimit, kLimit)};
}

/// Puts a body somewhere it can legally stand: inside the hull, on the deck.
///
/// Only ever reached by an enemy that begins its life buried or outside the
/// sector, which today is every enemy, because Sim spawns the wave at the
/// origin — a corner where the deck plate and two hull walls meet. Left alone,
/// resolve_penetration correctly ejects such a body sideways through the
/// nearest face and out of the sector, and the wave then consists entirely of
/// machines drifting in vacuum.
[[nodiscard]] vec3 legal_start(const VoxelWorld& world, vec3 position, vec3 half_extents) {
    const ivec3 extent = world.size_in_voxels();
    // The hull shell is one voxel thick on every side, so the interior starts
    // at 1 and the deck's top face is the plane y = 1.
    const f32 min_x = 1.0f + half_extents.x + kContactSkin;
    const f32 min_z = 1.0f + half_extents.z + kContactSkin;
    const f32 max_x = static_cast<f32>(extent.x - 1) - half_extents.x - kContactSkin;
    const f32 max_z = static_cast<f32>(extent.z - 1) - half_extents.z - kContactSkin;

    vec3 placed = position;
    placed.x = std::clamp(position.x, std::min(min_x, max_x), std::max(min_x, max_x));
    placed.z = std::clamp(position.z, std::min(min_z, max_z), std::max(min_z, max_z));
    placed.y = std::max(position.y, 1.0f + half_extents.y + kContactSkin);
    return placed;
}

/// Whether there is anything worth detonating on within the blast.
///
/// The deck is excluded because detonate() will not take it either, so a
/// Breacher standing in open ground on plating correctly reads as having
/// nothing to blow and keeps looking.
[[nodiscard]] bool solid_within_blast(const VoxelWorld& world, vec3 position) {
    const ivec3 centre{static_cast<i32>(std::floor(position.x)),
                       static_cast<i32>(std::floor(position.y)),
                       static_cast<i32>(std::floor(position.z))};
    constexpr i32 r = kBreachVoxelRadius;
    for (i32 dy = -r; dy <= r; ++dy) {
        for (i32 dz = -r; dz <= r; ++dz) {
            for (i32 dx = -r; dx <= r; ++dx) {
                const ivec3 target = centre + ivec3{dx, dy, dz};
                if (target.y <= 0) continue;
                if (world.contains(target) && is_solid(world.at(target))) return true;
            }
        }
    }
    return false;
}

/// Destroys voxels and hurts the player. The Breacher's whole contribution.
///
/// Deck plating at y = 0 is spared. A hole in the floor drops bodies out of
/// the sector into a fall nothing currently handles, and the flow field has no
/// way to express a pit — so until both exist, the play surface is structural.
void detonate(VoxelWorld& world, vec3 origin, vec3 player_position, EnemyTickResult& result) {
    const ivec3 centre{static_cast<i32>(std::floor(origin.x)),
                       static_cast<i32>(std::floor(origin.y)),
                       static_cast<i32>(std::floor(origin.z))};

    constexpr i32 r = kBreachVoxelRadius;
    // A rounded sphere rather than a cube: the corners of a 5x5x5 box are 1.7
    // voxels further out than its faces, and a blast that reaches further
    // diagonally reads as a bug in the destruction rather than as a shape.
    constexpr i32 r_sq = r * r + r;
    for (i32 dy = -r; dy <= r; ++dy) {
        for (i32 dz = -r; dz <= r; ++dz) {
            for (i32 dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy + dz * dz > r_sq) continue;
                const ivec3 target = centre + ivec3{dx, dy, dz};
                if (target.y <= 0) continue;
                if (!world.contains(target)) continue;
                if (!is_solid(world.at(target))) continue;
                world.damage(target, kBreachVoxelDamage);
            }
        }
    }

    const f32 distance = glm::length(player_position - origin);
    if (distance < kBreachPlayerRadius) {
        // Linear falloff, so closing the last two voxels is worth something to
        // the enemy and backing off is worth something to the player.
        const f32 scale = 1.0f - distance / kBreachPlayerRadius;
        result.damage_to_player += kBreachPlayerDamage * scale;
        ++result.attacks_landed;
    }
}

/// Raises one pillar of station plating just past the player, on the side they
/// are retreating toward.
///
/// Bulkhead, not Barricade: the Warden is rebuilding the station, and at
/// toughness 12 the wall costs the Salvage Rifle twelve shots to open. Walking
/// the pillar sideways each time means a stationary player is enclosed over
/// several builds instead of instantly, which leaves them a beat to move.
void raise_wall(VoxelWorld& world, vec3 from, vec3 player_position, EnemyAgent& agent) {
    vec3 away = flat_unit(player_position - from);
    if (glm::dot(away, away) < kEpsilon) away = vec3{0.0f, 0.0f, 1.0f};
    const vec3 lateral{-away.z, 0.0f, away.x};

    const f32 offset = static_cast<f32>(agent.build_index % 5 - 2);
    const vec3 spot = player_position + away * kWardenBuildDistance + lateral * offset;

    const i32 x = static_cast<i32>(std::floor(spot.x));
    const i32 z = static_cast<i32>(std::floor(spot.z));
    const i32 base_y = std::max(1, static_cast<i32>(std::floor(player_position.y)));

    bool placed = false;
    for (i32 i = 0; i < kWardenPillarHeight; ++i) {
        const ivec3 target{x, base_y + i, z};
        if (!world.contains(target)) continue;
        if (is_solid(world.at(target))) continue;
        world.set(target, Voxel::Bulkhead);
        placed = true;
    }
    // Only advance the pattern on a build that did something, so a Warden
    // pointed at a wall does not silently burn through its five offsets.
    if (placed) ++agent.build_index;
}

}  // namespace

// --- tuning -----------------------------------------------------------------

EnemyTuning tuning_for(EnemyKind kind) {
    EnemyTuning tuning;
    switch (kind) {
        case EnemyKind::Skitter:
            // Knee-high and faster than the player, so a line has to be held
            // rather than outrun. Two rifle shots kill it; the threat is the
            // count, not the individual.
            tuning.half_extents = {0.35f, 0.85f, 0.35f};
            tuning.move_speed = 13.0f;
            tuning.turn_rate = 12.0f;
            tuning.contact_range = 1.6f;
            tuning.contact_damage = 5.0f;
            tuning.attack_interval = 0.65f;
            break;

        case EnemyKind::Lancer:
            // Slower than the player on purpose: you can walk out of its band,
            // which is what makes the band a decision rather than a tax.
            tuning.half_extents = {0.45f, 1.30f, 0.45f};
            tuning.move_speed = 7.5f;
            tuning.turn_rate = 6.0f;
            tuning.contact_range = 1.8f;
            tuning.contact_damage = 0.0f;  // it shoots; see kLancerDamage
            tuning.attack_interval = kLancerInterval;
            tuning.preferred_range = kLancerRange;
            break;

        case EnemyKind::Bulwark:
            // Squat and broad rather than tall — it has to fit a doorway, and
            // a wide silhouette is what makes "get around it" legible. Turn
            // rate is the real stat: a player strafing at 11 voxels/s three
            // voxels away sweeps ~3.6 rad/s, so it cannot keep its plate on
            // you if you commit to circling.
            tuning.half_extents = {0.90f, 1.40f, 0.90f};
            tuning.move_speed = 4.5f;
            tuning.turn_rate = 1.6f;
            tuning.contact_range = 2.6f;
            tuning.contact_damage = 20.0f;
            tuning.attack_interval = 1.1f;
            break;

        case EnemyKind::Breacher:
            // Fast enough to be a timer once it has line of sight, fragile
            // enough that the timer can be cancelled by shooting it twice.
            tuning.half_extents = {0.50f, 1.00f, 0.50f};
            tuning.move_speed = 9.5f;
            tuning.turn_rate = 8.0f;
            tuning.contact_range = kBreachTriggerRange;
            tuning.contact_damage = 0.0f;  // it does not bite, it detonates
            tuning.attack_interval = 0.0f;
            break;

        case EnemyKind::Warden:
            // Two voxels wide, not three, and that is a hard constraint rather
            // than a look. NavField rounds a body up to whole voxel columns,
            // doorways are punched three wide, and a three-wide body fails the
            // stand test in them — with a 1.2 half-extent the field over a
            // default sector reaches 1578 cells instead of 3080 and the spawn
            // corner is not among them. The boss simply could not leave its
            // first room, and neither could anything else, because there is
            // one field and it is built for the largest body.
            tuning.half_extents = {1.00f, 1.40f, 1.00f};
            tuning.move_speed = 5.0f;
            tuning.turn_rate = 1.2f;
            tuning.contact_range = 3.0f;
            tuning.contact_damage = 25.0f;
            tuning.attack_interval = 1.4f;
            break;

        case EnemyKind::Count:
            break;
    }
    return tuning;
}

vec3 nav_body_half_extents() {
    vec3 largest{0.0f};
    for (u8 i = 0; i < static_cast<u8>(EnemyKind::Count); ++i) {
        const vec3 half = tuning_for(static_cast<EnemyKind>(i)).half_extents;
        largest = glm::max(largest, half);
    }
    return largest;
}

// --- armour -----------------------------------------------------------------

f32 armour_multiplier(EnemyKind kind, f32 facing_yaw, vec3 hit_direction) {
    if (kind != EnemyKind::Bulwark) return 1.0f;

    const vec3 incoming = flat_unit(hit_direction);
    if (glm::dot(incoming, incoming) < kEpsilon) return 1.0f;

    // +1 means the damage travels the way the body faces, i.e. it came from
    // behind. -1 means it came head-on into the plate.
    const f32 alignment = std::clamp(glm::dot(incoming, yaw_forward(facing_yaw)), -1.0f, 1.0f);
    if (alignment < 0.0f) {
        return kBulwarkFrontMultiplier + (1.0f - kBulwarkFrontMultiplier) * (alignment + 1.0f);
    }
    return 1.0f + (kBulwarkRearMultiplier - 1.0f) * alignment;
}

f32 damage_enemy(entt::registry& registry, entt::entity enemy, f32 raw_damage, vec3 hit_direction) {
    if (!registry.valid(enemy)) return 0.0f;
    if (!registry.all_of<Health>(enemy)) return 0.0f;

    f32 multiplier = 1.0f;
    if (registry.all_of<Enemy, Transform>(enemy)) {
        multiplier = armour_multiplier(
            registry.get<const Enemy>(enemy).kind, registry.get<const Transform>(enemy).yaw, hit_direction);
    }

    const f32 dealt = raw_damage * multiplier;
    registry.get<Health>(enemy).current -= dealt;
    return dealt;
}

// --- the tick ---------------------------------------------------------------

EnemyTickResult tick_enemies(
    entt::registry& registry, VoxelWorld& world, const NavField& nav, vec3 player_position, f32 dt) {
    EnemyTickResult result;

    // A non-positive or non-finite step advances nothing. Returning an empty
    // result rather than clamping keeps a paused frame from silently costing
    // the player health, and keeps a garbage dt from reaching physics.
    if (!(dt > 0.0f) || !std::isfinite(dt)) return result;

    auto view = registry.view<const Enemy, Transform, Velocity, Health>();
    for (const entt::entity entity : view) {
        Health& health = view.get<Health>(entity);
        if (!health.alive()) continue;

        const EnemyKind kind = view.get<const Enemy>(entity).kind;
        const EnemyTuning tuning = tuning_for(kind);
        Transform& transform = view.get<Transform>(entity);
        Velocity& velocity = view.get<Velocity>(entity);

        // Safe while iterating: EnemyAgent is not one of the view's pools, so
        // growing its storage cannot invalidate these iterators. Attaching it
        // here rather than at spawn keeps Sim's spawn path free of AI state
        // and lets a test tick a hand-built enemy.
        EnemyAgent& agent = registry.get_or_emplace<EnemyAgent>(entity);

        if (!agent.placed) {
            agent.placed = true;
            if (box_overlaps_solid(world, transform.position, tuning.half_extents)) {
                transform.position = legal_start(world, transform.position, tuning.half_extents);
                const DepenetrationResult settled =
                    resolve_penetration(world, transform.position, tuning.half_extents, kInstantEject);
                transform.position = settled.position;
            }
        }

        const vec3 to_player = player_position - transform.position;
        const f32 distance = glm::length(to_player);
        const vec3 player_heading = flat_unit(to_player);

        // One raycast per enemy per step, and only when something depends on
        // it. Line of sight is the gate on every ranged decision *and* on the
        // decision to stop using the flow field, so it cannot be skipped.
        const f32 sight_range = [kind] {
            switch (kind) {
                case EnemyKind::Lancer:
                    return kLancerRange;
                case EnemyKind::Warden:
                    return kWardenBuildRange;
                default:
                    return kDirectSteerRange;
            }
        }();
        const bool line_of_sight =
            distance <= sight_range &&
            has_line_of_sight(world, eye_of(transform.position, tuning), player_position);

        // "Engaged" means the enemy is doing its job on the player right now.
        // It is the exemption from the stuck rule: a Lancer holding its band
        // and a Bulwark grinding on your face are both stationary on purpose,
        // and reporting them as stuck would make the signal useless.
        const bool in_contact = distance <= tuning.contact_range;
        const bool engaged =
            in_contact ||
            (line_of_sight && tuning.preferred_range > 0.0f && distance <= tuning.preferred_range) ||
            (kind == EnemyKind::Warden && line_of_sight);

        // --- decide a heading ---
        vec3 heading{0.0f};
        bool holding = false;
        /// False only when the flow field has no route to the player from
        /// anywhere near this body. Local avoidance is switched off in that
        /// state: it refines a route, and there is no route to refine.
        bool routed = true;

        if (kind == EnemyKind::Lancer && line_of_sight && distance <= kLancerRange) {
            if (distance < kLancerRange * 0.7f) {
                // Too close to be a Lancer. Give ground rather than reposition
                // cleverly: backing straight off is readable from across the
                // room, which is what the player has to react to.
                heading = -player_heading;
            } else {
                holding = true;
            }
        } else if (line_of_sight && distance <= kDirectSteerRange) {
            // Same room, clear line: the flow field is coarser than the last
            // few voxels, and following it here makes an enemy stutter between
            // cell centres instead of closing.
            heading = player_heading;
        } else {
            heading = flat_unit(nav.direction_at(transform.position));
            if (glm::dot(heading, heading) < kEpsilon && !nav.reachable(transform.position)) {
                // No direction *and* no distance: this cell is off the field.
                // A zero direction with a valid distance means the goal cell
                // itself, where there is nowhere downhill to go — probing
                // outward there would find only uphill cells and walk the
                // enemy back out of the room it just entered.
                heading = unstick_heading(nav, transform.position);
                if (glm::dot(heading, heading) < kEpsilon) {
                    // Nothing within two cells has a route either: the player
                    // is sealed off. Drive at them anyway.
                    //
                    // This is the one place naive steering is the right
                    // answer, and it is the opposite of the soft-lock rather
                    // than a relapse into it. Standing still against a sealed
                    // room is the hang; walking into the wall puts a Breacher's
                    // nose on it, and a Breacher on a wall is a doorway in
                    // about a second. Meanwhile the body is by definition
                    // making no progress, so the stuck signal fires regardless
                    // and Sim still hears about it.
                    heading = player_heading;
                    routed = false;
                }
            }
        }

        // The flow field routes between cells; this bends the last two voxels
        // of that route around whatever the cell was too coarse to see.
        if (routed && !holding && glm::dot(heading, heading) > kEpsilon) {
            heading = avoid_locally(world, nav, transform.position, tuning.half_extents, heading, agent);
        }

        // Face what matters: the player when engaged, otherwise where it is
        // going. Slewed, not snapped — the gap between how fast a Bulwark
        // walks and how fast it turns is the flanking window.
        transform.yaw = slew_yaw(transform.yaw, engaged ? player_heading : heading, tuning.turn_rate, dt);

        // --- move ---
        vec3 next_velocity = velocity.value;
        if (holding) {
            next_velocity.x = 0.0f;
            next_velocity.z = 0.0f;
        } else {
            next_velocity.x = heading.x * tuning.move_speed;
            next_velocity.z = heading.z * tuning.move_speed;
        }
        // No acceleration curve. Machines, and one less axis to tune; the cost
        // is that an enemy changes direction instantly, which at these speeds
        // is not visible.
        next_velocity.y = std::max(next_velocity.y - kGravity * dt, -kTerminalFall);

        // De-penetrate before sweeping: move_and_slide explicitly does not, and
        // the geometry moves on steps where the enemy does not — a Warden
        // rebuilds a wall through the horde, a Breacher takes the deck out from
        // under it. Sealed means the body is inside geometry with nowhere to
        // go, which is the one state that neither movement nor navigation can
        // ever resolve.
        const DepenetrationResult ejected =
            resolve_penetration(world, transform.position, tuning.half_extents, kEjectSpeed * dt);
        transform.position = ejected.position;

        const vec3 previous_position = transform.position;
        const SweepResult moved = move_and_slide(world,
                                                 previous_position,
                                                 tuning.half_extents,
                                                 step_displacement(next_velocity, dt),
                                                 next_velocity);
        transform.position = moved.position;
        velocity.value = moved.velocity;
        if (moved.grounded && velocity.value.y < 0.0f) velocity.value.y = 0.0f;

        // The sector boundary is open vacuum and physics will not invent a wall
        // there, so the policy is ours: enemies are tethered to the sector.
        // Letting them drift is a wave that can never be cleared.
        if (moved.out_of_bounds) {
            transform.position = legal_start(world, transform.position, tuning.half_extents);
            velocity.value = vec3{0.0f};
        }

        // --- progress and the stuck rule ---
        const i32 nav_distance = nav.distance_at(transform.position);
        if (nav_distance >= 0 && (agent.best_distance < 0 || nav_distance < agent.best_distance)) {
            agent.best_distance = nav_distance;
            agent.no_progress_seconds = 0.0f;
        } else if (engaged) {
            agent.no_progress_seconds = 0.0f;
        } else {
            agent.no_progress_seconds += dt;
        }
        // Sealed is not a slow case of stuck, it is the terminal one: no
        // heading and no ejection can help a body with no free face. Report it
        // on the step it happens rather than four seconds later.
        agent.stuck = agent.no_progress_seconds >= kStuckSeconds || ejected.sealed;
        if (agent.stuck) result.any_enemy_stuck = true;

        if (ejected.sealed && kind != EnemyKind::Breacher) {
            health.current -= kSealedCrushDamagePerSecond * dt;
            if (!health.alive()) continue;
        }

        // Contact with geometry *and* no ground covered. Either alone is
        // normal — sliding along a wall makes progress, and an enemy can stall
        // for a step without touching anything.
        const f32 travelled = glm::length(flatten(transform.position - previous_position));
        const bool obstructed = moved.hit_x || moved.hit_z;
        if (!holding && obstructed && travelled < tuning.move_speed * dt * 0.35f) {
            agent.blocked_seconds += dt;
        } else {
            agent.blocked_seconds = 0.0f;
        }

        // --- act ---
        agent.attack_cooldown = std::max(0.0f, agent.attack_cooldown - dt);

        if (kind == EnemyKind::Breacher) {
            // Three triggers besides reaching the player, and all three are
            // anti-soft-lock measures wearing the archetype's clothes. Sealed:
            // entombed by a Warden, so blow out rather than be crushed.
            // Blocked: grinding on a barricade, so take the barricade — this
            // is what makes fortifications temporary rather than permanent.
            // Stuck next to masonry: the route has failed and there is a wall
            // to hand, which is the whole reason this archetype exists.
            if (in_contact || ejected.sealed || agent.blocked_seconds >= kBreachBlockedFuse ||
                (agent.stuck && solid_within_blast(world, transform.position))) {
                detonate(world, transform.position, player_position, result);
                // Sim reaps anything whose health has run out on the same tick,
                // so zeroing it here is the whole death path.
                health.current = 0.0f;
            }
            continue;
        }

        if (kind == EnemyKind::Warden) {
            agent.build_cooldown = std::max(0.0f, agent.build_cooldown - dt);
            if (line_of_sight && distance <= kWardenBuildRange && agent.build_cooldown <= 0.0f) {
                raise_wall(world, transform.position, player_position, agent);
                agent.build_cooldown = kWardenBuildInterval;
            }
        }

        if (tuning.preferred_range > 0.0f) {
            if (line_of_sight && distance <= tuning.preferred_range && agent.attack_cooldown <= 0.0f) {
                result.damage_to_player += kLancerDamage;
                ++result.attacks_landed;
                agent.attack_cooldown = tuning.attack_interval;
            }
            continue;
        }

        if (tuning.contact_damage > 0.0f && in_contact && agent.attack_cooldown <= 0.0f) {
            result.damage_to_player += tuning.contact_damage;
            ++result.attacks_landed;
            // Reset to the full interval rather than to interval minus the
            // overshoot. The drift is at most one step and it is what makes
            // the rate independent of the tick rate, which matters more.
            agent.attack_cooldown = tuning.attack_interval;
        }
    }

    return result;
}

}  // namespace df
