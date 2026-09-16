// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Global board client. Every call fails soft: the leaderboard is an extra, and
// a network problem must never disturb a flight.
import { cleanFlight, flightTag, character, cleanSeed } from './core.js';

// Set to the deployed Worker origin, e.g.
//   'https://driftfall-board.<subdomain>.workers.dev'
// The SAME origin must also be added to connect-src in index.html's CSP, or the
// browser blocks the request. Empty means the board is simply not configured.
export const ENDPOINT = '';
const TIMEOUT_MS = 6000;

export const available = () => Boolean(ENDPOINT);

async function call(path, { method = 'GET', body } = {}) {
  if (!ENDPOINT) throw new Error('The global board is not configured for this build.');
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  try {
    const response = await fetch(`${ENDPOINT}${path}`, {
      method,
      body: body === undefined ? undefined : JSON.stringify(body),
      headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
      // No cookies, no referrer: the board never needs to know who is asking.
      credentials: 'omit', referrerPolicy: 'no-referrer', signal: controller.signal, cache: 'no-store'
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || `The board replied ${response.status}.`);
    return data;
  } catch (error) {
    if (error.name === 'AbortError') throw new Error('The board did not respond.');
    throw error;
  } finally { clearTimeout(timer); }
}

function query(seed, mode, flight, pilot) {
  const f = cleanFlight(flight);
  return new URLSearchParams({ seed: cleanSeed(seed), mode, mix: f.mix,
    density: String(f.density), spread: String(f.spread), pilot: character(pilot).id }).toString();
}

export function board(seed, mode, flight, pilot) {
  return call(`/board?${query(seed, mode, flight, pilot)}`);
}
export function submit(name, ghost) {
  return call('/submit', { method: 'POST', body: { name, ghost } });
}
export function ghostById(id) {
  return call(`/ghost?id=${encodeURIComponent(String(id))}`);
}
// The board a run belongs to, so the UI can show the right one.
export const boardTag = (flight, pilot) => flightTag(flight, pilot);
