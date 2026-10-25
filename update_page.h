#pragma once
#include <WebServer.h>
// Static public page; every device operation requires the bearer password.
// Applications retain their own authenticated /v1/recover actuator-quiesce handler.
inline void registerUpdatePage(WebServer &server) {
  server.on("/update", HTTP_GET, [&server] {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.send(
        200, "text/html",
        R"HTML(<!doctype html><meta name="viewport" content="width=device-width"><title>Firmware update</title>
<style>body{font:18px system-ui;max-width:720px;margin:40px auto;padding:16px}input,button{font:inherit;margin:8px 0;padding:8px;max-width:95%}pre{white-space:pre-wrap;overflow-wrap:anywhere}label{display:block}</style>
<h1>Firmware update</h1><p>Use your device password to enter recovery. Stop connected equipment before restarting. Password stays in this page's memory; use a trusted local network.</p>
<label>Password <input id="password" type="password" autocomplete="off"></label>
<button id="connect">Connect / keep recovery open</button> <button id="recover">Enter recovery</button>
<p>In recovery, select the image's manifest and paste the exact source URL printed by <code>client.py serve --image ... --bind ...</code>. Keep that source running until verification finishes.</p>
<label>Manifest <input id="manifest" type="file" accept=".json"></label>
<label>Image source URL <input id="source" type="url" placeholder="http://computer:49195/image/checksum.bin"></label>
<button id="install">Install / resume image</button> <button id="status">Refresh status</button> <button id="launch">Launch verified firmware</button>
<pre id="result" role="status">Disconnected</pre><a href="/">Device home</a>
<script>
const el=id=>document.getElementById(id);let busy=false;
async function call(path,body,headers={}){const r=await fetch('/v1/'+path,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+el('password').value,...headers},body,redirect:'error',cache:'no-store'});const t=await r.text();if(!r.ok)throw Error(r.status+' '+t);return t;}
async function readStatus(){const s=JSON.parse(await call('status'));el('result').textContent=JSON.stringify(s,null,2);return s;}
function bind(id,fn){el(id).onclick=async()=>{if(busy)return;busy=true;try{await fn();}catch(e){el('result').textContent=e.message;}finally{busy=false;}};}
bind('status',readStatus);
bind('connect',async()=>{const s=await readStatus();if(s.role==='recovery'){await call('maintenance','');await readStatus();}});
bind('recover',async()=>{el('result').textContent=await call('recover','');el('result').textContent+='; wait for Wi-Fi, then Connect to recovery.';});
bind('install',async()=>{const f=el('manifest').files[0];if(!f)throw Error('Select the image manifest');const m=JSON.parse(await f.text());const u=new URL(el('source').value);if(u.protocol!=='http:'||u.username||u.password)throw Error('Use an HTTP source without credentials');await call('maintenance','');await call('begin','',{'X-Image-SHA256':m.sha256,'X-Image-Size':String(m.size),'X-Image-Name':m.name,'X-Image-Version':m.version});await call('download',u.href,{'Content-Type':'text/plain'});await readStatus();});
bind('launch',async()=>{el('result').textContent=await call('launch','');});
</script>)HTML");
  });
}
