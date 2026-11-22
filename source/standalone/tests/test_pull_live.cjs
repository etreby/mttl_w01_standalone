// Opt-in pull update in ota-live.js: mocked DOM and fetch, no device access.
// Load/refresh never POSTs; Check and Install only on clicks; Install needs
// confirm(); the install reuses the OTA reconnect/reload logic.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../ui/ota-live.js'),'utf8');
const ids=['ota-hint','ota-file-label','firmware-update','firmware-status','firmware-picker','firmware-selected','firmware-file','firmware-form','current-build-id','firmware-sha','ota-sha-label','pull-base','pull-save','pull-check','pull-install','pull-status'];
const offer={state:3,error:0,rom_ready:1,version:'2.2.0',build_id:'0123456789abcdef',size:434000,sha256:'ab'.repeat(32),expires_at:1900000000};
function setup(){
 const nodes=Object.fromEntries(ids.map(id=>[id,{id,textContent:'',value:'',files:[],hidden:false,disabled:false,click(){}}]));
 const env={nodes,requests:[],jobs:new Map(),serial:0,reloads:0,confirms:[],confirmAnswer:true,build:'1111111111111111',pull:{state:1,error:0,rom_ready:0},pullBase:'',config:{persistent:true,pull_base:''},
  respond:{}};
 const document={documentElement:{lang:'en'},getElementById:id=>nodes[id],activeElement:null,addEventListener(){},querySelector:()=>null};
 const json=(status,body)=>({ok:status<300,status,text:async()=>body===undefined?'':JSON.stringify(body),json:async()=>body});
 env.context={document,location:{reload(){env.reloads++;}},Date,localStorage:{getItem:()=>null,setItem(){},removeItem(){}},AbortController,Uint8Array,
  confirm:message=>{env.confirms.push(message);return env.confirmAnswer;},
  setTimeout:(fn,ms)=>{if(ms===250){Promise.resolve().then(fn);return 0;}env.jobs.set(++env.serial,{fn,ms});return env.serial;},clearTimeout:id=>env.jobs.delete(id),
  fetch:async(url,options={})=>{const method=options.method||'GET';env.requests.push({url,method,options});
   if(env.respond[url])return env.respond[url](options);
   if(url==='/api/firmware/status')return json(200,{ota_ready:true,build_id:env.build});
   if(url==='/api/config')return json(200,env.config);
   if(url==='/api/pull/status')return json(200,env.pull);
   if(url==='/api/pull/base'){const b=[...options.body];if(b.length&&b[0]!==10&&b[0]!==172&&b[0]!==192)return json(400);env.config={persistent:true,pull_base:b.length?`http://${b.slice(0,4).join('.')}:${b[4]*256+b[5]}`:''};env.pull={state:b.length?1:0,error:0,rom_ready:0};return json(202,{accepted:true,pending:true});}
   if(url==='/api/pull/check'){env.pull=offer;return json(200,offer);}
   if(url==='/api/pull/install')return json(200,{accepted:true,pending:true});
   throw Error('unexpected '+url);}};
 return env;
}
const flush=async()=>{for(let i=0;i<20;i++)await new Promise(setImmediate);};
const posts=env=>env.requests.filter(r=>r.method==='POST');
async function step(env,ms){const entry=[...env.jobs.entries()].find(([,job])=>job.ms===ms);assert.ok(entry,'missing timer '+ms);env.jobs.delete(entry[0]);await entry[1].fn();await flush();}
async function run(){
 let checks=0;const ok=(...a)=>{assert.ok(...a);checks++;},eq=(...a)=>{assert.equal(...a);checks++;};
 // Load: status/config only, never a POST; disabled server disables Check.
 let env=setup();env.pull={state:0,error:0,rom_ready:0};vm.runInNewContext(source,env.context);await flush();
 eq(posts(env).length,0);ok(env.requests.some(r=>r.url==='/api/pull/status'));eq(env.nodes['pull-check'].disabled,true);eq(env.nodes['pull-install'].hidden,true);
 ok(/Off/.test(env.nodes['pull-status'].textContent));
 // Invalid URLs never reach the device.
 for(const bad of ['https://192.168.0.10:8080','http://192.168.0.10','http://192.168.0.010:80','http://192.168.0.300:80','http://192.168.0.10:70000','http://host:80']){
  env.nodes['pull-base'].value=bad;await env.nodes['pull-save'].onclick();await flush();eq(posts(env).length,0,bad);ok(/Invalid URL/.test(env.nodes['pull-status'].textContent));}
 // Valid URL: 6-byte body, confirmed by GET /api/config.
 env.nodes['pull-base'].value=' http://192.168.0.10:8080/ ';await env.nodes['pull-save'].onclick();await flush();
 let p=posts(env);eq(p.length,1);eq(p[0].url,'/api/pull/base');eq(p[0].options.headers['Content-Type'],'application/octet-stream');eq(JSON.stringify([...p[0].options.body]),'[192,168,0,10,31,144]');
 eq(env.nodes['pull-check'].disabled,false);ok(/Not checked/.test(env.nodes['pull-status'].textContent));
 // The device enforces RFC 1918: a public address is refused (HTTP 400 shown).
 env.nodes['pull-base'].value='http://8.8.8.8:80';await env.nodes['pull-save'].onclick();await flush();ok(/HTTP 400/.test(env.nodes['pull-status'].textContent));
 // Visibility/tab refresh still never POSTs.
 const before=posts(env).length;vm.runInNewContext(source,env.context);await flush();eq(posts(env).length,before);
 // Check: one POST, offer shown with version/build/size/expiry, Install appears.
 await env.nodes['pull-check'].onclick();await flush();p=posts(env);eq(p.at(-1).url,'/api/pull/check');eq(p.length,before+1);
 const text=env.nodes['pull-status'].textContent;ok(/Available/.test(text)&&/2\.2\.0/.test(text)&&/0123456789abcdef/.test(text)&&/434000 bytes/.test(text)&&/2030-03-17/.test(text),text);
 eq(env.nodes['pull-install'].hidden,false);eq(env.nodes['pull-install'].disabled,false);
 // Install refused at confirm(): nothing sent. The prompt warns about outlets.
 env.confirmAnswer=false;await env.nodes['pull-install'].onclick();await flush();eq(posts(env).length,before+1);ok(/outlets turn off/.test(env.confirms[0])&&/0123456789abcdef/.test(env.confirms[0]));
 // Install confirmed: POST names the displayed build_id, then reconnect; new build reloads.
 env.confirmAnswer=true;await env.nodes['pull-install'].onclick();await flush();p=posts(env);eq(p.at(-1).url,'/api/pull/install');eq(p.at(-1).options.body,'0123456789abcdef');
 ok(/Downloading and installing/.test(env.nodes['firmware-status'].textContent));eq(env.nodes['pull-check'].disabled,true);eq(env.nodes['firmware-update'].disabled,true);
 await step(env,5000);eq(env.reloads,0);env.build='2222222222222222';await step(env,3000);eq(env.reloads,1);eq(env.context.location.hash,'#firmware');
 eq(posts(env).filter(r=>r.url==='/api/pull/install').length,1,'no automatic install retry');
 // Failed install: device answers with the old build and pull state "failed"; stop, show the error, no retry.
 env=setup();env.pull=offer;vm.runInNewContext(source,env.context);await flush();eq(env.nodes['pull-install'].hidden,false);
 await env.nodes['pull-install'].onclick();await flush();env.pull={state:5,error:10,rom_ready:1,...offer,state:5,error:10};
 await step(env,5000);eq(env.reloads,0);ok(/uncertain/.test(env.nodes['firmware-status'].textContent)&&/digest/.test(env.nodes['firmware-status'].textContent));
 eq([...env.jobs.values()].filter(j=>j.ms===3000).length,0,'reconnect loop stopped');eq(posts(env).filter(r=>r.url==='/api/pull/install').length,1);
 // Verifier self-test failure is reported.
 env=setup();env.pull={state:5,error:2,rom_ready:-1};vm.runInNewContext(source,env.context);await flush();ok(/signature/.test(env.nodes['pull-status'].textContent)&&/self-test failed/.test(env.nodes['pull-status'].textContent));
 console.log(JSON.stringify({checks,failed:0,scope:'Mock pull-update dashboard flow (load, save URL, check, confirm, install, reconnect, failure); no device access'}));
}
run().catch(e=>{console.error(e);process.exitCode=1;});
