// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
import http from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const port=Number(process.env.PORT||8080);
const types={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.svg':'image/svg+xml','.json':'application/json','.wasm':'application/wasm'};
http.createServer(async(req,res)=>{
  try{
    const url=new URL(req.url,'http://localhost');let pathname=decodeURIComponent(url.pathname);
    // Prefix mode exercises the same relative-path behavior as GitHub project Pages.
    if(pathname.startsWith('/driftfall/'))pathname=pathname.slice('/driftfall'.length);
    let file=path.resolve(root,'.'+pathname);
    if(file!==root&&!file.startsWith(root+path.sep)){res.writeHead(403);res.end('Forbidden');return;}
    if((await stat(file)).isDirectory())file=path.join(file,'index.html');
    const bytes=await readFile(file);res.writeHead(200,{'Content-Type':types[path.extname(file)]||'application/octet-stream','Cache-Control':'no-store','X-Content-Type-Options':'nosniff','Permissions-Policy':'camera=(self), microphone=()'});res.end(bytes);
  }catch{res.writeHead(404,{'Content-Type':'text/plain'});res.end('Not found');}
}).listen(port,'127.0.0.1',()=>console.log(`DRIFTFALL: http://localhost:${port}/driftfall/`));
