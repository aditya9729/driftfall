# DRIFTFALL leaderboard

A Cloudflare Worker + D1 database backing a global board for the static game.

## What is actually verified

This matters more than the board itself, so it is stated plainly.

The Worker imports the **game's own** `validateReplay` from
`rift-runners/src/replay.js`. A submission is rejected unless it is a genuine,
well-formed ghost: monotonic timestamps, no sample exceeding the speed limit,
the route actually finished, and (for Rift Run) a finish time above the
physical floor `COURSE_LENGTH / (48 * 1.08)`. Server and client can never drift
apart, because they run the same function.

**What that gives you:** nobody can post a 3-second lap, a run that stops
halfway, a teleporting ship, or a hand-typed number. Every board entry has a
ghost attached that anyone can download and watch.

**What it does not give you:** the run is not re-simulated from inputs, so a
determined person could still craft a physically-plausible ghost by hand. And
for Swarm Run the *score* is claimed rather than recomputed, because a position
ghost does not record kills or combo. Rift Run ranks by **time**, which is the
part the validator constrains, and the board says so in its own response.

Closing that last gap means recording inputs instead of positions so the server
can deterministically re-run the simulation and derive the score itself. The
simulation is already deterministic and fixed-step, so this is a real option,
not a fantasy — it is just a larger change to the ghost format.

## Deploy

You need a Cloudflare account. All of this is free tier.

```sh
npm install -g wrangler          # or: npx wrangler <cmd>
wrangler login                   # opens a browser, one time

cd leaderboard
wrangler d1 create driftfall-board
#   -> copy the printed database_id into wrangler.toml

wrangler d1 execute driftfall-board --remote --file=./schema.sql
wrangler deploy
#   -> prints https://driftfall-board.<your-subdomain>.workers.dev
```

Then wire the game to it — **both** of these, or the browser will block the call:

1. `rift-runners/src/leaderboard.js` — set `ENDPOINT` to the printed URL.
2. `rift-runners/index.html` — add that exact origin to `connect-src` in the CSP.

Use the exact origin. Do not widen the CSP to `https://*.workers.dev`: that
would let *any* Cloudflare Worker, including someone else's, talk to the page.

## Endpoints

| Method | Path | Purpose |
|---|---|---|
| `GET`  | `/board?seed=&mode=&mix=&density=&spread=&pilot=` | Top 20 for one course build |
| `GET`  | `/ghost?id=` | The stored ghost, so you can race a leader |
| `POST` | `/submit` | `{name, ghost}`; validates, stores, returns your rank |

A board is keyed by `(seed, mode, flightTag(flight, pilot))`, so a custom flight
ranks against its own course and never against the standard one.

## Limits and privacy

- Writes are accepted only from `ALLOWED_ORIGIN` (set in `wrangler.toml`).
- 6 submissions per minute, per submitter.
- Rate limiting stores a **salted, truncated SHA-256** of the connecting
  address for 60 seconds and sweeps it on every write. No address is stored.
- Display names are uppercased and stripped to `[A-Z0-9 ]`, max 16 characters.
- No accounts, no cookies, no camera data — the ghost is flight positions only.
