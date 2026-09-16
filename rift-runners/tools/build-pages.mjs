// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
// Assemble the Cloudflare Pages bundle: the static game plus a same-origin API.
//
// The API is the SAME leaderboard/src/worker.js that runs as a standalone
// Worker; it is copied in with its imports repointed at the game's own modules,
// which are already part of the deployment. Server and client therefore share
// one validator by construction, not by convention.
import { mkdir, rm, cp, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const game = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const root = path.resolve(game, '..');
const out = path.join(game, 'build', 'pages');

await rm(out, { recursive: true, force: true });
await mkdir(out, { recursive: true });

for (const file of ['index.html', 'styles.css', 'icon.svg', 'LICENSE']) {
  await cp(path.join(game, file), path.join(out, file));
}
await cp(path.join(game, 'src'), path.join(out, 'src'), { recursive: true });
await writeFile(path.join(out, '.nojekyll'), '');

// _worker.js sits at the bundle root, so ./src reaches the game modules the
// worker imports. Pages "advanced mode": this file handles every request and is
// never served as a static asset, unlike a functions/ directory, which wrangler
// resolves against the working directory rather than the asset directory.
const worker = (await readFile(path.join(root, 'leaderboard', 'src', 'worker.js'), 'utf8'))
  .replaceAll('../../rift-runners/src/', './src/');
const entry = `${worker.replace('export default {', 'const api = {')}
// Everything under /api is the board; everything else is the static game.
export default {
  fetch(request, env) {
    return new URL(request.url).pathname.startsWith('/api')
      ? api.fetch(request, env)
      : env.ASSETS.fetch(request);
  }
};
`;
await writeFile(path.join(out, '_worker.js'), entry);

console.log(`Pages bundle ready: ${path.relative(root, out)}`);
