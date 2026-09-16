// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// DRIFTFALL leaderboard. Runs on Cloudflare Workers with a D1 database.
//
// The server imports the GAME'S OWN replay validator, so the rules a submission
// must satisfy cannot drift away from the rules the client enforces.
import { validateReplay, MAX_REPLAY_BYTES, lastDistance } from '../../rift-runners/src/replay.js';
import { COURSE_LENGTH, cleanSeed, character, cleanFlight, flightTag, MODES } from '../../rift-runners/src/core.js';

// A race cannot physically be finished faster than this. Top speed is 48 u/s
// scaled by the fastest pilot (1.08), and energy makes sustained boost
// impossible, so real finishes land far above it. Kept deliberately loose: it
// exists to reject the absurd, not to second-guess a good run.
const FLOOR_SECONDS = COURSE_LENGTH / (48 * 1.08);
const MAX_NAME = 16;
const BOARD_LIMIT = 20;
const RATE_PER_MINUTE = 6;

const json = (body, status, origin) => new Response(JSON.stringify(body), {
  status,
  headers: {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Access-Control-Allow-Origin': origin,
    'Vary': 'Origin'
  }
});

// A display name is shown to other people: letters, digits and spaces only.
function cleanName(value) {
  const name = String(value ?? '').toUpperCase().replace(/[^A-Z0-9 ]/g, '').replace(/\s+/g, ' ').trim().slice(0, MAX_NAME);
  return name || 'ANONYMOUS';
}

// A short salted hash for rate limiting. Never stores an address.
async function throttleKey(request, salt) {
  const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
  const bytes = new TextEncoder().encode(`${salt}:${ip}`);
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest).slice(0, 8)].map(b => b.toString(16).padStart(2, '0')).join('');
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const allowed = env.ALLOWED_ORIGIN || '';
    const sent = request.headers.get('Origin');
    // Same-origin requests either send our own origin or none at all. A
    // cross-origin writer is only ever honoured if it matches ALLOWED_ORIGIN.
    const sameOrigin = !sent || sent === url.origin;
    const origin = sent && (sent === allowed || sent === url.origin) ? sent : '';

    // Mounted at /api on the game's own origin (same-origin, so CORS is moot),
    // and also servable at the root as a standalone Worker.
    const path = url.pathname.replace(/^\/api(?=\/|$)/, '') || '/';

    if (request.method === 'OPTIONS') {
      return new Response(null, { status: 204, headers: {
        'Access-Control-Allow-Origin': origin,
        'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type',
        'Access-Control-Max-Age': '86400',
        'Vary': 'Origin'
      }});
    }

    try {
      if (request.method === 'GET' && path === '/board') return await board(url, env, origin);
      if (request.method === 'GET' && path === '/ghost') return await ghost(url, env, origin);
      if (request.method === 'POST' && path === '/submit') {
        // Only the game's own origin may write.
        if (!origin && !sameOrigin) return json({ error: 'Submissions are only accepted from the game.' }, 403, '');
        return await submit(request, env, origin);
      }
      return json({ error: 'Not found.' }, 404, origin);
    } catch (error) {
      return json({ error: 'The leaderboard is unavailable.', detail: String(error?.message || error) }, 500, origin);
    }
  }
};

function boardKey(url) {
  const seed = cleanSeed(url.searchParams.get('seed'));
  const mode = MODES.includes(url.searchParams.get('mode')) ? url.searchParams.get('mode') : 'race';
  const tag = flightTag(cleanFlight({
    mix: url.searchParams.get('mix'),
    density: url.searchParams.get('density'),
    spread: url.searchParams.get('spread')
  }), url.searchParams.get('pilot'));
  return { seed, mode, tag };
}

async function board(url, env, origin) {
  const { seed, mode, tag } = boardKey(url);
  // Race ranks by time. Swarm survives a fixed 90s, so it ranks by score.
  const order = mode === 'swarm' ? 'score DESC, elapsed ASC' : 'elapsed ASC, score DESC';
  const { results } = await env.DB.prepare(
    `SELECT id, name, pilot, elapsed, score, created FROM runs
     WHERE seed = ? AND mode = ? AND tag = ? ORDER BY ${order} LIMIT ?`
  ).bind(seed, mode, tag, BOARD_LIMIT).all();
  return json({
    seed, mode, tag,
    entries: (results || []).map((row, i) => ({ rank: i + 1, ...row })),
    // Say exactly what the ranking means. See README "What is actually verified".
    verified: mode === 'swarm'
      ? 'Ghost replay validated; survival time enforced. Score is claimed, not recomputed.'
      : 'Ghost replay validated: the route was finished and every sample is within the speed limit.'
  }, 200, origin);
}

async function ghost(url, env, origin) {
  const id = String(url.searchParams.get('id') || '').slice(0, 40);
  const row = await env.DB.prepare('SELECT ghost FROM runs WHERE id = ?').bind(id).first();
  if (!row) return json({ error: 'No such run.' }, 404, origin);
  return json(JSON.parse(row.ghost), 200, origin);
}

async function submit(request, env, origin) {
  const raw = await request.text();
  if (raw.length > MAX_REPLAY_BYTES + 512) return json({ error: 'Submission is too large.' }, 413, origin);

  let payload;
  try { payload = JSON.parse(raw); } catch { return json({ error: 'Malformed submission.' }, 400, origin); }

  // The same validator the game uses, in strict mode: an unfinished run is
  // rejected here exactly as an unfinished import is rejected in the browser.
  let ghostData;
  try { ghostData = validateReplay(payload.ghost); }
  catch (error) { return json({ error: error.message }, 422, origin); }

  if (ghostData.mode !== 'swarm') {
    if (lastDistance(ghostData) < COURSE_LENGTH - 8) return json({ error: 'That run did not finish the route.' }, 422, origin);
    if (ghostData.elapsed < FLOOR_SECONDS) return json({ error: 'That finish time is not physically possible.' }, 422, origin);
  }

  const key = await throttleKey(request, env.ALLOWED_ORIGIN || 'driftfall');
  const since = Date.now() - 60_000;
  const recent = await env.DB.prepare('SELECT COUNT(*) AS n FROM throttle WHERE who = ? AND created > ?')
    .bind(key, since).first();
  if ((recent?.n ?? 0) >= RATE_PER_MINUTE) return json({ error: 'Too many submissions. Try again shortly.' }, 429, origin);

  const tag = flightTag(ghostData.flight, ghostData.character);
  const id = crypto.randomUUID();
  const now = Date.now();
  await env.DB.batch([
    env.DB.prepare(`INSERT INTO runs (id, seed, mode, tag, pilot, name, elapsed, score, ghost, created)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`)
      .bind(id, ghostData.seed, ghostData.mode, tag, character(ghostData.character).id,
            cleanName(payload.name), ghostData.elapsed, Math.round(ghostData.score), JSON.stringify(ghostData), now),
    env.DB.prepare('INSERT INTO throttle (who, created) VALUES (?, ?)').bind(key, now),
    env.DB.prepare('DELETE FROM throttle WHERE created < ?').bind(since)
  ]);

  const order = ghostData.mode === 'swarm' ? 'score DESC, elapsed ASC' : 'elapsed ASC, score DESC';
  const { results } = await env.DB.prepare(
    `SELECT id FROM runs WHERE seed = ? AND mode = ? AND tag = ? ORDER BY ${order} LIMIT 200`
  ).bind(ghostData.seed, ghostData.mode, tag).all();
  const rank = (results || []).findIndex(r => r.id === id) + 1;
  return json({ id, rank: rank || null, of: (results || []).length }, 201, origin);
}
