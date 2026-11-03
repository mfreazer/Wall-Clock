// Exercise the actual embedded script against a minimal DOM and mocked HTTP API.
const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const html = fs.readFileSync(require('path').join(__dirname, '../include/web_ui.h'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const initial = {count:60,offset:0,clockwise:true,idle:false,period:4.5,low:10,high:50,timezone:'UTC0',am:[255,100,20],pm:[180,40,255],minute:[20,150,255],second:[255,30,40],background:[255,255,255]};
let remote = {...initial}, tag='"boot-0"', posts=[], rejectNext=false, needsRepair=false, bufferAvailable=true;
const elements = {};
for (const [name, value] of Object.entries(initial)) {
  elements[name] = {name, type:Array.isArray(value)?'color':typeof value==='boolean'?'checkbox':typeof value==='number'?'number':'text', value:'', checked:false, addEventListener(event, fn){this[event]=fn}};
}
const form = {elements, querySelectorAll(){return Object.values(elements)}};
const nodes = {'form':form, '#status':{}, '#message':{}, '#save':{}, '#reload':{}, '#colors':{append(){}}};
const document = {querySelector(s){return nodes[s]}, createElement(){return {append(){}}}};
function response(status, data, etag=tag) {return {ok:status>=200&&status<300,status,headers:{get(k){return k==='ETag'?etag:null}},json:async()=>data,text:async()=>String(data)}}
async function fetch(url, options={}) {
 if(url==='/api/status') return response(200,{connected:true,synchronized:true,configurationNeedsRepair:needsRepair,ledBufferAvailable:bufferAvailable});
 if(options.method==='POST') {
  posts.push(options);
  if(rejectNext || options.headers['If-Match']!==tag) {rejectNext=false;return response(412,'stale')}
  remote=JSON.parse(options.body);tag='"boot-1"';return response(200,{saved:true});
 }
 return response(200,{...remote});
}
const context=vm.createContext({document,fetch,setInterval(){},console});
vm.runInContext(script,context);
const flush=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 await flush();
 assert.strictEqual(nodes['#save'].disabled,false);
 assert.strictEqual(elements.count.value,60);
 needsRepair=true;await vm.runInContext('poll()',context);
 assert.match(nodes['#status'].textContent,/Stored settings invalid.*LEDs off/);
 bufferAvailable=false;await vm.runInContext('poll()',context);
 assert.match(nodes['#status'].textContent,/Stored settings invalid/);assert.match(nodes['#status'].textContent,/LED buffer unavailable/);
 needsRepair=false;await vm.runInContext('poll()',context);
 assert.match(nodes['#status'].textContent,/LED buffer unavailable/);assert.doesNotMatch(nodes['#status'].textContent,/Time acquired/);
 bufferAvailable=true;await vm.runInContext('poll()',context);assert.match(nodes['#status'].textContent,/Time acquired/);
 elements.count.value='24';elements.count.input();assert.strictEqual(elements.offset.max,23);
 elements.offset.value='2';
 await form.onsubmit({preventDefault(){}});
 assert.strictEqual(posts.length,1);assert.strictEqual(posts[0].headers['If-Match'],'"boot-0"');
 assert.strictEqual(JSON.parse(posts[0].body).count,24);assert.strictEqual(Object.keys(JSON.parse(posts[0].body)).length,13);
 // An external/button update is detected without destroying unsaved edits.
 elements.count.value='30';remote.idle=true;tag='"boot-2"';
 await vm.runInContext('poll()',context);
 assert.strictEqual(elements.count.value,'30');assert.strictEqual(nodes['#save'].disabled,true);
 await form.onsubmit({preventDefault(){}});assert.strictEqual(posts.length,1);
 await nodes['#reload'].onclick();assert.strictEqual(elements.idle.checked,true);assert.strictEqual(nodes['#save'].disabled,false);
 // A change between the last poll and the POST must also fail closed.
 tag='"boot-3"';await form.onsubmit({preventDefault(){}});assert.strictEqual(posts.length,2);assert.strictEqual(nodes['#save'].disabled,true);
 console.log('Embedded web UI tests passed');
})().catch(e=>{console.error(e);process.exitCode=1});
