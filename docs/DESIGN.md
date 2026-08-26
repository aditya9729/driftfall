<!-- Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0 -->

# DRIFTFALL — Game Design

## 1. Pitch

You are a salvage-mech pilot boarding derelict stations drifting in the debris
belt. Each run is one procedurally generated station sector, built entirely out
of voxels. The loop is **Salvage → Fortify → Survive**, repeated until you die
or extract.

## 2. The core loop

```
        ┌──────────────────────────────────────────┐
        │                                          │
        ▼                                          │
   ┌─────────┐  timer or   ┌──────────┐  cleared   │
   │  PREP   │────────────▶│  ASSAULT │───────────▶│
   │         │  skip early │          │            │
   │ mine    │             │ hold the │      ┌─────┴─────┐
   │ build   │             │ line     │      │  CLEARED  │
   │ deploy  │             │          │      │ loot,     │
   └─────────┘             └──────────┘      │ read next │
                                 │           └───────────┘
                                 │ you die
                                 ▼
                             ┌────────┐
                             │ DEFEAT │
                             └────────┘
```

**Prep** is where the game actually lives. You have a shrinking window — 75
seconds at wave 1, floored at 22 by wave 12 — to mine voxels for salvage and
spend that salvage on barricades, reinforced walls, and turret-bots. Skipping
prep early converts the unused time directly into salvage, so being ready is
rewarded rather than merely faster.

**Assault** removes building entirely. Whatever you built is what you have.
Enemies trickle in rather than arriving as a block, so the pressure builds.

The tension is a single question, asked every wave: *do I spend this salvage on
walls that might not be where the swarm comes from, or on a turret that will
definitely shoot something?*

## 3. Why the destruction matters

Most horde shooters have static cover. DRIFTFALL's cover is voxels, which means:

- Cover **degrades**. Shooting through a wall to hit an enemy destroys the wall.
- Cover is **creatable**. Salvage becomes barricades in the prep phase.
- Cover is **attackable**. The Breacher archetype exists specifically to
  detonate against player-built walls, which makes your fortifications a
  target rather than a solution.
- Sightlines are **authored by the player**, not the level designer. The Breach
  Shotgun's real job is not killing — it is making a new window.

This is what makes touch controls viable. Precise aim is hard with thumbs;
choosing *where the fight happens* is not.

## 4. Weapons and the active reload

Three starting weapons, each defined by the ratio between enemy damage and
voxel damage:

| Weapon | Enemy dmg | Voxel dmg | Role |
|---|---|---|---|
| Salvage Rifle | 24 | 1 | The default. Kills things, barely marks walls. |
| Breach Shotgun | 96 | 4 | Opens sightlines. The architecture tool. |
| Mining Lance | 9 | 6 | Harvests a sector fast. Nearly useless in a fight. |

Every weapon uses **active reload**, lifted openly from Gears of War:

```
  reload bar    0 ─────────────────────────────────── 1
                        [ PERFECT ][   GOOD   ]
                        0.62  0.70          0.86
```

- Tap inside **PERFECT** → instant reload *and* a 30 % damage bonus for the
  whole magazine.
- Tap inside **GOOD** → instant reload, no bonus.
- Tap anywhere else → **JAM**: the reload stretches by an extra 1.15 s.

One judged tap per reload. Mashing the button cannot farm perfects — without
that rule the entire risk/reward of the mechanic evaporates, and it is the
first thing every playtester tries.

Running a magazine dry clears the bonus, so the perfect reload is something you
maintain, not something you bank. There is no auto-reload: the player chooses
when to be vulnerable.

## 5. The enemy roster

Every enemy is a machine. No human bodies anywhere in DRIFTFALL.

| Archetype | Unlocks | Cost | Role |
|---|---|---|---|
| **Skitter** | wave 1 | 1 | Fast, fragile, arrives in numbers. The texture of a horde. |
| **Lancer** | wave 3 | 3.5 | Ranged. Punishes standing still, forces cover use. |
| **Breacher** | wave 5 | 5 | Detonates on contact, destroys voxels — including yours. |
| **Bulwark** | wave 7 | 8 | Armoured front plate. Must be flanked or breached. |
| **Warden** | every 5th | 40 | Boss. *Rebuilds the station's walls against you.* |

The Warden is the design keystone: a boss that uses the player's own core verb
(voxel manipulation) as its weapon. It seals the corridor you were kiting
through and forces you to breach your way out.

Archetypes unlock one at a time. That is the difference between a player
learning the roster and a player being buried by it.

## 6. Difficulty: budget, then elites

Waves are composed from a points budget rather than hand-authored:

```
budget(w) = 10w + 8 · 1.16^(w-1)
```

A linear floor keeps early waves from being trivially thin; the geometric term
provides the late-game curve. The director spends that budget on enemies from
the currently unlocked pool, always seeding a large share into Skitters.

Past **220 concurrent bodies** the budget stops buying population and starts
buying **power** — every enemy in the wave scales up in health and damage
instead. That cap is derived from the frame budget, not from taste: ~220
animated, pathing enemies is what an iPhone 12 can hold alongside the voxel
world inside 16.6 ms.

This is the mechanism that lets wave 50 be genuinely harder than wave 30
without the frame rate falling apart. `WaveSpec::elite_multiplier` carries it,
and a test asserts that bodies × power always accounts for the full budget.

Everything is deterministic in the run seed, which is what makes daily
challenges, replays, and reproducible bug reports possible later.

## 7. The economy

| Material | Mine yield | Build cost |
|---|---|---|
| Ore | 4 | — |
| Bulkhead | 2 | — |
| Hull plate / Ice | 1 | — |
| Barricade | **0** | 2 |
| Reinforced | **0** | 9 |

Tearing down your own fortifications refunds nothing. Otherwise
build-and-refund becomes a free action and the economy stops meaning anything.

A turret-bot costs 60 salvage — roughly fifteen ore voxels, or most of one prep
phase early on. It has to hurt.

## 8. Session shape

A run is 10–15 minutes: eight to twelve waves, then an extraction choice.
That is the phone-native session length, and it is also how long a device stays
under its thermal ceiling.

## 9. Controls

Touch-first. Two **floating** sticks, not fixed ones — the movement stick
spawns wherever your left thumb lands. Fixed sticks demand that the player look
at their thumbs, and in a horde shooter looking away from the screen is death.

Reload is a *tap* on the aiming half of the screen. The active-reload window is
measured in frames; no input that time-critical should require moving a thumb.

Generous aim assist is assumed and non-negotiable. The design is about
positioning and architecture, not about flick precision.

## 10. Rating and audience

Machines-only enemies target **ESRB Teen / App Store 12+** rather than 17+.
This is a business decision as much as a creative one: it roughly doubles the
addressable audience, removes an entire class of technology from the schedule,
and removes any question about the App Store review outcome.

## 11. What is explicitly out of scope for v1

- Real-time multiplayer. Netcode for a destructible voxel world is a project of
  its own. v1 ships single-player with seeded runs and asynchronous
  leaderboards; co-op is a post-launch conversation.
- Infinite worlds. Sectors are bounded. See
  [ARCHITECTURE.md](ARCHITECTURE.md#why-sectors-are-bounded).
- Player-facing voxel building outside the prep phase.
- Any monetisation mechanic that touches the age rating (no loot boxes).
