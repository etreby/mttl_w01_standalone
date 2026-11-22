/* FWR-only OTA. The device verifies format, checksum and flash readback. */
(function(){
 'use strict';
 const el=id=>document.getElementById(id);let ready=false,busy=false,result=null,detail='';
const words=['Keep power on. Supported: standalone FWR, stock 1.0.66 or local-patched 1.0.68. Restoring removes standalone web/direct HA.','Firmware file (.fwr)','Update','Ready to update','Not ready to update','Select a valid .fwr file.','Uploading… Keep power on.','Update completed. Reconnecting to Wi-Fi.','Result uncertain. Reconnect and check before retrying.','Update and restart?\nRestoring 1.0.66/1.0.68 removes standalone web/direct HA. Use original provisioning or backend connectivity afterwards.','Restore completed. Check original provisioning or backend connectivity.','This firmware is already installed. No update or reboot is needed.','Update completed, but automatic reconnection timed out. Check the device IP and refresh the page.','Expected SHA-256 (from OTA server, optional)','SHA-256 must be 64 hex characters.','Downloading and installing… Keep power on. The device reboots when done.'];
 let currentBuild=null,reconnecting=false,reconnectUntil=0;
 const pendingKey='mttl.ota.pending-build';
 function remember(id){try{if(id)localStorage.setItem(pendingKey,id);else localStorage.removeItem(pendingKey);}catch(_){}}
 function render(){const w=words;el('ota-hint').textContent=w[0];el('ota-file-label').textContent=w[1];el('firmware-update').textContent=w[2];el('ota-sha-label').textContent=w[13];el('firmware-status').textContent=w[result===null?(ready?3:4):result]+(detail?' '+detail:'');el('firmware-picker').disabled=busy||reconnecting;el('firmware-update').disabled=busy||reconnecting||!ready;el('firmware-selected').textContent=el('firmware-file').files[0]?.name||'No file selected';}
 async function reconnect(){
  if(Date.now()>=reconnectUntil){reconnecting=false;result=12;render();return;}
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);
  try{
   const response=await fetch('/api/firmware/status',{cache:'no-store',credentials:'omit',signal:controller.signal});
   if(!response.ok)throw Error('Reconnect');const status=await response.json();
   if(/^[0-9a-f]{16}$/.test(status.build_id||'')&&currentBuild&&status.build_id!==currentBuild){
    location.hash='#firmware';location.reload();return;
   }
   // Pull install: the device answers again without rebooting only after a failure.
   if(pulling){const p=await pullGet('/api/pull/status');if(p.state===5){pulling=reconnecting=false;result=8;detail=pullText(p);remember(null);render();pullShow(p);return;}}
  }catch(_){}finally{clearTimeout(timeout);}
  setTimeout(reconnect,3000);
 }
 el('firmware-picker').onclick=()=>el('firmware-file').click();el('firmware-file').onchange=()=>{result=null;detail='';render();};
 el('firmware-form').onsubmit=async event=>{
  event.preventDefault();if(busy||!ready)return;
  const file=el('firmware-file').files[0];
  if(!file||file.size<44||file.size>0x6a014||!file.name.toLowerCase().endsWith('.fwr')){result=5;render();return;}
  const sha=el('firmware-sha').value.trim().toLowerCase();if(sha&&!/^[0-9a-f]{64}$/.test(sha)){result=14;render();return;}
  if(!confirm(words[9]))return;
  const expected=file.name.match(/-build-([0-9a-f]{16})\.fwr$/i)?.[1]?.toLowerCase();remember(expected);
  busy=true;result=6;detail='';render();const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),180000);
  try{
   const response=await fetch('/api/firmware/upload',{method:'POST',headers:{'Content-Type':'application/x-mttl-fwr',...sha&&{'X-MTTL-SHA256':sha}},body:file,signal:controller.signal,credentials:'omit'});
   const raw=await response.text();let body;try{body=JSON.parse(raw);}catch(_){}
   if(response.ok&&body?.already_installed===true){result=11;detail='';remember(null);return;}
   if(!response.ok||!body?.written||!body?.activated){
    const message=raw.slice(0,2048).replace(/<[^>]*>/g,' ').replace(/[\x00-\x1f\x7f]/g,' ').replace(/\s+/g,' ').trim().slice(0,400);
    detail='HTTP '+response.status+(message?' · '+message:' · Empty response');throw Error('Server response');
   }
   result=body.restore_version?10:7;detail=body.restore_version||'';ready=false;if(body.restore_version)remember(null);
   else{reconnecting=true;reconnectUntil=Date.now()+120000;setTimeout(reconnect,5000);}
  }
  catch(error){result=8;ready=false;if(!detail)detail=error.name==='AbortError'?'Timeout (180 s)':'Network/connection error: '+String(error.message||error).slice(0,160);}
  finally{clearTimeout(timer);busy=false;render();}
 };
 // Opt-in pull update (docs/PULL_UPDATE_EN.md): nothing is checked or installed without a click.
 const pullStates=['Off','Not checked','Checking','Available','Up to date','Failed','Installing'],pullErrors=',malformed,signature,expired,time,not_newer,size,http,redirect,busy,digest,install'.split(',');
 let pulling=false,offer=null,pullBusy=false,pullState=0;
 async function pullGet(url,options={}){
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),options.wait||8e3);
  try{const response=await fetch(url,{cache:'no-store',credentials:'omit',signal:controller.signal,...options}),raw=await response.text();if(!response.ok)throw Error('HTTP '+response.status);return raw?JSON.parse(raw):{};}
  finally{clearTimeout(timer);}
 }
 function pullText(p){return pullStates[p.state]+(p.state===5?': '+pullErrors[p.error]:'')+(p.rom_ready<0?' · verifier self-test failed':'');}
 function pullShow(p){
  if(p){pullState=p.state;offer=p.state===3?p:null;el('pull-status').textContent=pullText(p)+(offer?` · ${p.version} · build ${p.build_id} · ${p.size} bytes · expires ${new Date(p.expires_at*1e3).toISOString()}`:'');}
  const off=pullBusy||busy||reconnecting;el('pull-install').hidden=!offer;el('pull-install').disabled=off;el('pull-check').disabled=off||!pullState;el('pull-save').disabled=off;
 }
 // http://<IPv4>:<port> (trailing / removed by the caller) as 6 bytes (IPv4, port big endian), empty = off; the device enforces RFC 1918.
 function pullBase(text){const n=(/^http:\/\/(\d+)\.(\d+)\.(\d+)\.(\d+):(\d+)$/.exec(text)||[]).slice(1).map(Number);return !text?[]:text==='http://'+n.slice(0,4).join('.')+':'+n[4]&&n[4]<65536&&n.every((x,i)=>i>3||x<256)?[...n.slice(0,4),n[4]>>8,n[4]&255]:null;}
 async function pullAction(work){if(pullBusy)return;pullBusy=true;pullShow();try{await work();}catch(error){el('pull-status').textContent=error.name==='AbortError'?'Timeout':String(error.message);}finally{pullBusy=false;pullShow();}}
 if(el('pull-status')){
  el('pull-save').onclick=()=>pullAction(async()=>{
   const text=el('pull-base').value.trim().replace(/\/$/,''),body=pullBase(text);if(!body)throw Error('Invalid URL');
   await pullGet('/api/pull/base',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:new Uint8Array(body)});
   for(let n=0;n<12;n++){await new Promise(resolve=>setTimeout(resolve,250));if(((await pullGet('/api/config').catch(()=>0))||{}).pull_base===text)return pullShow(await pullGet('/api/pull/status'));}
   throw Error('Save not confirmed');
  });
  el('pull-check').onclick=()=>pullAction(async()=>{el('pull-status').textContent='Checking…';const p=await pullGet('/api/pull/check',{method:'POST',wait:30000});pullShow(p);});
  el('pull-install').onclick=()=>pullAction(async()=>{
   const o=offer;if(!o||!confirm('Install firmware '+o.version+' (build '+o.build_id+')?\nAll outlets turn off while the device reboots. Keep power on.'))return;
   await pullGet('/api/pull/install',{method:'POST',headers:{'Content-Type':'text/plain'},body:o.build_id});
   remember(o.build_id);offer=null;pulling=reconnecting=true;result=15;detail='';ready=false;render();reconnectUntil=Date.now()+300000;setTimeout(reconnect,5000);
  });
 }
 async function pullRefresh(){if(!el('pull-status')||pullBusy)return;try{const c=await pullGet('/api/config');if(document.activeElement!==el('pull-base'))el('pull-base').value=c.pull_base||'';pullShow(await pullGet('/api/pull/status'));}catch(_){}}
 let statusPending=false,statusTimer=null;
 async function refreshStatus(){
  if(statusPending||busy||result===7||result===8||result===10||result===12||result===15)return;
  statusPending=true;clearTimeout(statusTimer);
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),8000);
  try{
  const response=await fetch('/api/firmware/status',{cache:'no-store',credentials:'omit',signal:controller.signal});
  if(!response.ok)throw Error('Status');const status=await response.json();
  if(typeof status.ota_ready!=='boolean'||!/^[0-9a-f]{16}$/.test(status.build_id||''))throw Error('Status');
  ready=status.ota_ready===true;const build=/^[0-9a-f]{16}$/.test(status.build_id||'')?status.build_id:null;
  currentBuild=build;if(el('current-build-id'))el('current-build-id').textContent=build||'—';
  let expected;try{expected=localStorage.getItem(pendingKey);}catch(_){}
  if(expected&&build){detail='Build verification: '+(expected===build?'OK · ':'NOT MATCHED · ')+build;if(expected===build)remember(null);}
  }catch(_){ready=false;statusTimer=setTimeout(refreshStatus,3000);}
  finally{clearTimeout(timeout);statusPending=false;render();}
  pullRefresh();
 }
 document.querySelector?.('[aria-controls="firmware"]')?.addEventListener('click',refreshStatus);
 document.addEventListener?.('visibilitychange',()=>{if(!document.hidden)refreshStatus();});
 refreshStatus();render();
})();
