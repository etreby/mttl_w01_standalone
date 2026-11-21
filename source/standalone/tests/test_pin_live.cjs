// Built device bundle: optional PIN is sent on POSTs, kept in sessionStorage
// only, and a set/clear request never reports success on an HTTP error.
// Requires build_assets.py --ota-test output; set MTTL_REQUIRE_UI_ASSETS=1 to fail when absent.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const file=path.join(__dirname,'../results/network-ota-test/sdk/ui_assets.h');
if(!fs.existsSync(file)){if(process.env.MTTL_REQUIRE_UI_ASSETS==='1')throw Error('Missing '+file);console.log(JSON.stringify({skipped:'bundle not built'}));process.exit(0);}
const [html,i18n,live]=[...fs.readFileSync(file,'utf8').matchAll(/\[\]=\{([0-9,]+)\}/g)].map(m=>zlib.gunzipSync(Buffer.from(m[1].split(',').map(Number))).toString('utf8'));
function element(id){return {id,attributes:{},style:{},dataset:{},classList:{add(){},remove(){},toggle(){},contains:()=>false},files:[],children:[],value:'',textContent:'',hidden:false,disabled:false,checked:false,
 setAttribute(n,v){this.attributes[n]=String(v);},getAttribute(n){return this.attributes[n]??null;},removeAttribute(n){delete this.attributes[n];},addEventListener(){},append(...n){this.children.push(...n);},appendChild(c){this.children.push(c);return c;},
 replaceChildren(...n){this.children=n;},remove(){},focus(){},click(){},querySelector:()=>element(),querySelectorAll:()=>[],closest:()=>null,scrollIntoView(){},select(){}};}
const ids=new Map(),byId=id=>{if(!ids.has(id))ids.set(id,element(id));return ids.get(id);};
const session=new Map(),calls=[];let configStatus=200,devicePin='1234',applyWorks=true;
const config={pin_set:true,applied:7,device:'MTTL-W01',channels:['SW 1','SW 2','SW 3','SW 4'],broker:'',port:1883,user:'',topic:'mttl',enabled:false,language:1,persistent:true,button_lock:0,timer_minutes:[0,0,0,0],protection:{enabled:7,temperature:90,current:16000,power:3000}};
const reply=(status,body)=>({ok:status<300,status,json:async()=>body,text:async()=>JSON.stringify(body)});
const fetch=async(url,options={})=>{calls.push({url,options});
 if(url==='/api/status')return reply(200,{state:5,uptime_ms:1000,ip:1,hardware_ready:true});
 if(url==='/api/device')return reply(200,{channels:[{},{},{},{}],hardware_ready:true});
 if(url==='/api/config'){
  if(!options.method)return reply(200,{...config});
  if(configStatus>=300)return reply(configStatus,{});
  if(devicePin&&options.headers?.['X-MTTL-PIN']!==devicePin)return reply(403,{});
  const next=options.headers?.['X-MTTL-New-PIN'];
  if(next&&applyWorks){devicePin=next==='clear'?'':next;config.pin_set=!!devicePin;config.applied++;} /* worker applies asynchronously */
  return reply(202,{accepted:true,pending:true});}
 if(url==='/api/control')return reply(options.headers?.['X-MTTL-PIN']==='1234'?200:403,{});
 return new Promise(()=>{});};
const document={documentElement:element('html'),body:element('body'),title:'',hidden:false,getElementById:byId,querySelector:()=>element(),querySelectorAll:()=>[],createElement:()=>element(),createTextNode:t=>({textContent:t}),addEventListener(){}};
const context={document,console,location:{hash:'',search:'',reload(){}},navigator:{language:'en-US'},localStorage:{getItem:()=>null,setItem(){},removeItem(){}},
 sessionStorage:{getItem:k=>session.get(k)??null,setItem:(k,v)=>session.set(k,String(v))},
 setTimeout:(fn,ms)=>{if(ms===250)setImmediate(fn);return 0;},clearTimeout(){},setInterval:()=>0,clearInterval(){},fetch,AbortController,TextEncoder,TextDecoder,URLSearchParams,
 EventSource:function(){return {addEventListener(){},close(){}};},confirm:()=>false,alert(){},Date,Intl,Math,JSON,Promise,Map,Set,Array,Object,Number,String,Error,RegExp,Uint8Array};
context.globalThis=context;context.window=context;vm.createContext(context);
const flush=async(n=20)=>{for(let i=0;i<n;i++)await new Promise(setImmediate);};
(async()=>{
 assert.match(html,/id="pin-form"/);assert.ok(!/PIN 저장</.test(html));
 session.set('mttl.pin','1234');
 vm.runInContext(i18n,context);vm.runInContext(live,context);await flush();
 assert.equal(byId('pin').value,'1234','PIN restored from sessionStorage');
 // GETs never carry the PIN; POSTs do.
 assert.ok(calls.filter(c=>!c.options.method).every(c=>!c.options.headers?.['X-MTTL-PIN']));
 // Invalid new PIN: rejected client-side, nothing posted.
 let posts=calls.filter(c=>c.options.method).length;
 byId('pin-new').value='12a';await byId('pin-form').onsubmit({preventDefault(){}});await flush();
 assert.match(byId('pin-status').textContent,/4–12 digits/);assert.equal(calls.filter(c=>c.options.method).length,posts);
 // Set a new PIN: current PIN in X-MTTL-PIN, new one in X-MTTL-New-PIN.
 byId('pin-new').value='5678';await byId('pin-form').onsubmit({preventDefault(){}});await flush();
 let post=calls.filter(c=>c.options.method==='POST'&&c.url==='/api/config').at(-1);
 assert.equal(post.options.headers['X-MTTL-PIN'],'1234');assert.equal(post.options.headers['X-MTTL-New-PIN'],'5678');
 assert.equal(byId('pin-status').textContent,'PIN updated.');assert.equal(byId('pin').value,'5678');assert.equal(session.get('mttl.pin'),'5678');assert.equal(byId('pin-new').value,'');
 // Empty new PIN requests removal.
 await byId('pin-form').onsubmit({preventDefault(){}});await flush();
 post=calls.filter(c=>c.options.method==='POST'&&c.url==='/api/config').at(-1);assert.equal(post.options.headers['X-MTTL-New-PIN'],'clear');assert.equal(post.options.headers['X-MTTL-PIN'],'5678');
 // Device accepted but never applied (e.g. flash save failed): not reported as saved.
 byId('pin').value='';byId('pin-new').value='2222';applyWorks=false;await byId('pin-form').onsubmit({preventDefault(){}});await flush(80);
 assert.notEqual(byId('pin-status').textContent,'PIN updated.');assert.equal(byId('pin').value,'');assert.equal(config.pin_set,false);applyWorks=true;
 // A 403 (wrong current PIN) is a definite failure: never reported as saved.
 devicePin='4321';byId('pin').value='0000';byId('pin-new').value='9999';await byId('pin-form').onsubmit({preventDefault(){}});await flush();
 assert.notEqual(byId('pin-status').textContent,'PIN updated.');assert.equal(byId('pin').value,'0000');assert.equal(devicePin,'4321');
 configStatus=403;byId('pin').value='0000';byId('pin-new').value='9999';await byId('pin-form').onsubmit({preventDefault(){}});await flush();
 assert.notEqual(byId('pin-status').textContent,'PIN updated.');assert.equal(byId('pin').value,'0000');
 console.log(JSON.stringify({passed:true,checks:20,scope:'Built ota-test bundle, fake DOM/network: PIN header on POST only, sessionStorage, set/clear/403'}));
})().catch(e=>{console.error(e);process.exitCode=1;});
