// Run the actual offline phone flow, including network failures and partial saves.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('main/apps/home/phone_setup.html','utf8');
const script=html.match(/<script>\n([\s\S]*?)<\/script>/)[1];
assert.ok(!/https?:\/\//.test(html),'the captive page must not depend on an internet origin');
const elements=new Map(),requests=[],timers=new Map();let nextTimer=1;
class Element {
 constructor(){this.value='';this.textContent='';this.style={};this.dataset={};this.options=[];this.classList={toggle(){}};this.type='password';this.files=[];}
 add(o){this.options.push(o)} replaceChildren(...o){this.options=o;this.value=''}
 setAttribute(k,v){this[k]=v} removeAttribute(k){delete this[k]} focus(){} setPointerCapture(){}
 get selectedOptions(){return this.options.filter(o=>o.value===this.value)}
 getContext(){return {drawImage(){},fillRect(){},putImageData(){},getImageData:()=>({data:new Uint8ClampedArray(96*150*4).fill(255)}),createImageData:()=>({data:new Uint8ClampedArray(96*150*4)})}}
 toBlob(cb,type){assert.equal(type,'image/jpeg');cb(new Blob([new Uint8Array(20000)],{type}))}
}
const get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id)};
class Image {constructor(){this.naturalWidth=240;this.naturalHeight=320}set src(v){this._src=v;setImmediate(()=>this.onload?.())}get src(){return this._src}}
let state='idle',saved=false,failShare=false,denyFinish=false;
const context=vm.createContext({document:{getElementById:get,createElement:()=>new Element()},window:{__T:314159,scrollTo(){}},Option:class{constructor(text,value){this.text=text;this.value=value;this.dataset={}}},Image,TextEncoder,Uint8Array,Uint8ClampedArray,Blob,AbortController,URL:{createObjectURL:()=> 'blob:fixture',revokeObjectURL(){}},setTimeout(fn,ms){const id=nextTimer++;timers.set(id,{fn,ms});return id},clearTimeout:id=>timers.delete(id),fetch:async(url,options)=>{
 requests.push({url,options});let result={},ok=true;
 if(url.startsWith('/api/profile')&&!options.method)result={name:'测试',signature:'已有签名',address:'原地址',city:'自定义城市',lat:'22.3',lon:'113.6',avatar:true,share_image:true};
 if(url.startsWith('/api/setup'))result={state,saved_network:saved,ssid:'Test Wi-Fi'};
 if(url.startsWith('/api/networks'))result=[{ssid:'Test Wi-Fi',secure:true}];
 if(url.startsWith('/api/share')&&failShare){ok=false;result={error:'store'}}
 if(url.startsWith('/api/finish')&&denyFinish){ok=false;result={error:'network_required'}}
 return {ok,json:async()=>result};
}});
vm.runInContext(script,context);
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const posts=()=>requests.filter(r=>r.options.method==='POST');
(async()=>{
 await flush();await flush();
 await get('save').onclick();assert.equal(posts().length,0,'first boot cannot complete without Wi-Fi');
 get('network').value='Test Wi-Fi';get('password').value='short';await get('connect').onclick();assert.equal(posts().length,0);
 get('password').value='  secret with spaces  ';await get('connect').onclick();
 let req=posts().at(-1);assert.equal(req.url,'/api/connect?t=314159');assert.equal(JSON.parse(req.options.body).password,'  secret with spaces  ');
 state='failed';await context.poll();assert.equal(get('connect').disabled,false);assert.match(get('networkStatus').textContent,/检查密码/);
 await get('connect').onclick();state='connected';await context.poll();assert.equal(get('step2').hidden,false);assert.equal(get('password').value,'');
 get('name').value='新名字';get('address').value='工作室地址';
 get('file').files=[{size:1000}];await get('file').onchange();
 get('shareFile').files=[{size:1000}];await get('shareFile').onchange();
 failShare=true;await get('save').onclick();assert.equal(posts().filter(r=>r.url.startsWith('/api/finish')).length,0,'failed image must not close hotspot');assert.equal(get('save').disabled,false);
 req=posts().find(r=>r.url.startsWith('/api/profile'));let profile=JSON.parse(req.options.body);assert.equal(profile.name,'新名字');assert.equal(profile.address,'工作室地址');assert.equal(profile.city,'自定义城市');assert.equal(profile.lat,'22.3','preserve custom weather coordinates');
 req=posts().find(r=>r.url.startsWith('/api/avatar'));assert.equal(req.options.body.byteLength,1800);
 failShare=false;denyFinish=true;await get('save').onclick();assert.match(get('saveStatus').textContent,/连接 Wi-Fi/);
 denyFinish=false;await get('save').onclick();assert.equal(get('step3').hidden,false);assert.equal(get('doneName').textContent,'新名字');
 // A reload during verification resumes polling instead of allowing a duplicate request.
 state='connecting';saved=true;await context.load();assert.equal(get('connect').disabled,true);assert.equal(get('reuse').hidden,false);
 console.log('Phone setup: first-run gate, Wi-Fi retry/space preservation, optional profile, custom city, both images, partial-save recovery, resume: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
