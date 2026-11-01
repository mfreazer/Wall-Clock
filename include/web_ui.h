#pragma once
const char WEB_UI[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ring clock</title><style>body{background:#10151d;color:#e9eff7;font:16px system-ui;max-width:720px;margin:50px auto;padding:20px}h1{font-size:40px}fieldset{border:1px solid #394455;border-radius:12px;margin:20px 0;padding:20px}label{display:flex;justify-content:space-between;gap:20px;margin:12px 0}input{max-width:230px}button{padding:12px 22px;background:#8ce5cb;border:0;border-radius:8px;cursor:pointer}#status{color:#8ce5cb}</style><h1>Ring clock</h1><p>Local light, precisely timed.</p><p id="status" role="status">Connecting…</p><p id="message" role="alert"></p><form id="settings"><fieldset><legend>Ring & time</legend><label>Physical LEDs <input name="count" type="number" min="1" max="300" required></label><label>Zero offset <input name="offset" type="number" min="0" required></label><label>Clockwise <input name="clockwise" type="checkbox"></label><label>POSIX time zone <input name="timezone" maxlength="96" required></label><p>Use a tested POSIX TZ rule, not an IANA name. Invalid rules may silently display UTC.</p></fieldset><fieldset><legend>Light</legend><label>Period (seconds) <input name="period" type="number" min="3" max="6" step="0.1" required></label><label>Low brightness (%) <input name="low" type="number" min="0" max="100" step="0.1" required></label><label>High brightness (%) <input name="high" type="number" min="0" max="100" step="0.1" required></label><label>Idle illumination <input name="idle" type="checkbox"></label><div id="colors"></div></fieldset><button id="save" disabled>Save settings</button> <button id="reload" type="button">Reload settings (discard edits)</button></form><script>
const form=document.querySelector('form'),status=document.querySelector('#status'),message=document.querySelector('#message');
const save=document.querySelector('#save'),reload=document.querySelector('#reload');
let revision=null,stale=false;
for(const [key,label] of Object.entries({am:'Hour · AM',pm:'Hour · PM',minute:'Minute',second:'Second',background:'Idle'})){let l=document.createElement('label');l.textContent=label;let i=document.createElement('input');i.type='color';i.name=key;l.append(i);document.querySelector('#colors').append(l)}
function offsetLimit(){const count=Number(form.elements.count.value);form.elements.offset.max=Number.isInteger(count)&&count>0?count-1:0}
form.elements.count.addEventListener('input',offsetLimit);
async function refresh(){
 save.disabled=true;
 try{
  let r=await fetch('/api/config',{cache:'no-store'});if(!r.ok)throw Error('Cannot read settings');
  let tag=r.headers.get('ETag');if(!tag)throw Error('Clock did not provide a settings revision');
  let c=await r.json();for(const [k,v] of Object.entries(c)){let i=form.elements[k];if(i)i.type==='checkbox'?i.checked=v:i.value=Array.isArray(v)?'#'+v.map(n=>n.toString(16).padStart(2,'0')).join(''):v}
  revision=tag;stale=false;offsetLimit();message.textContent='';save.disabled=false;
 }catch(e){revision=null;message.textContent=e.message}
}
async function poll(){
 try{
  let r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();let s=await r.json();
  status.textContent=s.synchronized?(s.connected?'Time acquired · last sync age unknown':'Offline · holding acquired time'):'Waiting for network time · LEDs off';
  let configResponse=await fetch('/api/config',{cache:'no-store'});if(!configResponse.ok)throw Error();
  let tag=configResponse.headers.get('ETag');if(!tag)throw Error();
  if(revision && tag!==revision){stale=true;save.disabled=true;message.textContent='Settings changed on the clock. Reload to discard your edits before saving.'}
 }catch(e){status.textContent='Clock unreachable'}
}
reload.onclick=refresh;
form.onsubmit=async e=>{
 e.preventDefault();if(!revision||stale)return;
 let c={};for(const i of form.querySelectorAll('input'))c[i.name]=i.type==='checkbox'?i.checked:i.type==='number'?Number(i.value):i.type==='color'?[1,3,5].map(p=>parseInt(i.value.slice(p,p+2),16)):i.value;
 if(c.low>=c.high){message.textContent='Low brightness must be less than high brightness';return}
 save.disabled=true;
 try{
  let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json','If-Match':revision},body:JSON.stringify(c)});
  if(r.status===412||r.status===428){stale=true;throw Error('Settings changed on the clock. Reload to discard your edits before saving.')}
  if(!r.ok)throw Error(await r.text());
  let tag=r.headers.get('ETag');if(!tag){stale=true;throw Error('Settings saved, but revision is missing. Reload before saving again.')}
  revision=tag;message.textContent='Settings saved';
 }catch(e){message.textContent=e.message}finally{save.disabled=stale}
};
refresh();setInterval(poll,10000);
</script></html>)HTML";
