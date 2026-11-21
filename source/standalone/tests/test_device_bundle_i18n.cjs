// Built device bundles must not reference messages that the build pruned,
// reference element IDs missing from the page, or contain non-English text.
// Requires build_assets.py output; set MTTL_REQUIRE_UI_ASSETS=1 to fail when absent.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const builds={normal:'../results/sdk/ui_assets.h','ota-test':'../results/network-ota-test/sdk/ui_assets.h'};
function assets(file){
 const header=fs.readFileSync(file,'utf8'),arrays=[...header.matchAll(/\[\]=\{([0-9,]+)\}/g)].map(m=>zlib.gunzipSync(Buffer.from(m[1].split(',').map(Number))).toString('utf8'));
 assert.equal(arrays.length,3,'Expected /, /i18n.js and /live.js');return {html:arrays[0],i18n:arrays[1],live:arrays[2]};
}
function element(id,attributes={}){
 const node={id,attributes,style:{},dataset:{},classList:{add(){},remove(){},toggle(){},contains:()=>false},files:[],children:[],value:'',textContent:'',hidden:false,disabled:false,
  setAttribute(name,value){this.attributes[name]=String(value);},getAttribute(name){return this.attributes[name]??null;},removeAttribute(name){delete this.attributes[name];},
  addEventListener(){},append(...nodes){this.children.push(...nodes);},appendChild(child){this.children.push(child);return child;},replaceChildren(...nodes){this.children=nodes;},
  remove(){},focus(){},click(){},querySelector:()=>element(),querySelectorAll:()=>[],closest:()=>null,scrollIntoView(){},select(){}};
 return node;
}
function run(name,bundle){
 const ids=new Map(),errors=[];
 const declared=new Set([...bundle.html.matchAll(/\sid="([^"]+)"/g)].map(m=>m[1]));
 const byId=id=>{if(!ids.has(id))ids.set(id,element(id));return ids.get(id);};
 const lookup=id=>{if(!declared.has(id)){errors.push('Missing element #'+id);return null;}return byId(id);};
 const tagged=[...bundle.html.matchAll(/<([a-z0-9]+)\s([^>]*)>/g)].map(([, ,attrs])=>{const a={};for(const [,k,v] of attrs.matchAll(/([\w-]+)(?:="([^"]*)")?/g))a[k]=v??'';return a.id?Object.assign(byId(a.id),{attributes:{...byId(a.id).attributes,...a}}):element(undefined,a);});
 const select=selector=>{const attr=selector.match(/^\[([\w-]+)\]$/)?.[1];if(attr)return tagged.filter(n=>attr in n.attributes);return Array.from({length:5},()=>element());};
 const document={documentElement:element('html',{lang:'en'}),body:element('body'),title:'',hidden:false,getElementById:lookup,querySelector:s=>select(s)[0]||element(),querySelectorAll:select,
  createElement:tag=>element(undefined,{tag}),createTextNode:text=>({textContent:text}),addEventListener(){}};
 document.documentElement.lang='en';
 const context={document,console:{log(){},warn(){},error:(...a)=>errors.push(a.join(' '))},location:{hash:'',search:'',reload(){}},navigator:{language:'en-US'},
  localStorage:{getItem:()=>null,setItem(){},removeItem(){}},setTimeout:()=>0,clearTimeout(){},setInterval:()=>0,clearInterval(){},
  fetch:()=>new Promise(()=>{}),AbortController,TextEncoder,TextDecoder,URLSearchParams,EventSource:function(){return {addEventListener(){},close(){}};},
  confirm:()=>false,alert(){},Date,Intl,Math,JSON,Promise,Map,Set,Array,Object,Number,String,Error,RegExp};
 context.globalThis=context;context.window=context;context.self=context;
 vm.createContext(context);
 vm.runInContext(bundle.i18n,context,{filename:name+'/i18n.js'});
 const text=context.MttlI18n.text;
 const missing=new Set();context.MttlI18n.text=(key,args)=>{try{return text(key,args);}catch(error){missing.add(key);throw error;}};
 for(const [,key] of bundle.live.matchAll(/\bt\(\s*["'](\w+)["']\s*[,)]/g))if(context.MttlI18n.messages[key]===undefined)missing.add(key);
 for(let n=0;n<=6;n++)if(context.MttlI18n.messages['netState'+n]===undefined)missing.add('netState'+n);
 for(const [part,source] of Object.entries(bundle)){
  assert.ok(!/[\u1100-\u11ff\u3130-\u318f\uac00-\ud7a3]/.test(source),name+': non-English text in '+part);
  assert.ok(!/data-i18n|simulat|draft/i.test(source),name+': preview or translation markup in '+part);
 }
 try{vm.runInContext(bundle.live,context,{filename:name+'/live.js'});}catch(error){errors.push(String(error.message||error));}
 assert.deepEqual([...missing],[],name+': missing messages');
 assert.deepEqual(errors.filter(e=>/Missing translation/.test(e)),[],name+': runtime translation errors');
 assert.deepEqual(errors,[],name+': runtime errors');
 return tagged.length;
}
const results={};
for(const [name,relative] of Object.entries(builds)){
 const file=path.join(__dirname,relative);
 if(!fs.existsSync(file)){if(process.env.MTTL_REQUIRE_UI_ASSETS==='1')throw Error('Missing '+file+'; run build_assets.py'+(name==='ota-test'?' --ota-test':''));results[name]='skipped (not built)';continue;}
 results[name]=run(name,assets(file))+' elements checked';
}
console.log(JSON.stringify({passed:true,results,scope:'Built device bundles: English-only, every t() key exists, every looked-up element ID exists, and live.js loads without errors; fake DOM, no network'}));
