import { readdir,readFile } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
const sources=(await readdir('src')).filter(p=>p.endsWith('.js'));
for(const file of sources){const r=spawnSync(process.execPath,['--check',path.join('src',file)],{stdio:'inherit'});if(r.status)process.exit(r.status);}
const html=await readFile('index.html','utf8');
if(/src="https?:\/\//.test(html))throw new Error('External script on initial page load');
if(!html.includes('camera-consent'))throw new Error('Camera consent missing');
console.log(`Syntax OK: ${sources.length} modules. Initial HTML has no external scripts.`);
