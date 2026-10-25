const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(__dirname+'/update_page.h','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements={};for(const id of ['password','connect','recover','manifest','source','install','status','launch','result'])elements[id]={value:'',files:[],textContent:''};
elements.password.value='test-password';elements.source.value='http://192.0.2.1:49195/image/test.bin';
elements.manifest.files=[{text:async()=>JSON.stringify({sha256:'a'.repeat(64),size:4096,name:'test',version:'1.0'})}];
let requests=[],role='main_selftest',reject=false;
const context={document:{getElementById:id=>elements[id]},URL,fetch:async(path,options)=>{
 requests.push({path,options});return {ok:!reject,status:reject?401:200,text:async()=>reject?'unauthorized':path==='/v1/status'?JSON.stringify({role}):'ok'};
}};
vm.runInNewContext(script,context);
(async()=>{
 await elements.connect.onclick();assert.deepEqual(requests.map(r=>r.path),['/v1/status']);
 requests=[];await elements.recover.onclick();assert.equal(requests[0].path,'/v1/recover');
 role='recovery';requests=[];await elements.connect.onclick();assert.deepEqual(requests.map(r=>r.path),['/v1/status','/v1/maintenance','/v1/status']);
 requests=[];await elements.install.onclick();assert.deepEqual(requests.map(r=>r.path),['/v1/maintenance','/v1/begin','/v1/download','/v1/status']);
 assert.equal(requests[1].options.headers['X-Image-SHA256'],'a'.repeat(64));assert.equal(requests[2].options.body,elements.source.value);
 for(const r of requests){assert.equal(r.options.headers.Authorization,'Bearer test-password');assert.equal(r.options.redirect,'error');assert.ok(!r.path.includes('test-password'));}
 requests=[];elements.source.value='http://user:pass@example.test/image.bin';await elements.install.onclick();assert.equal(requests.length,0);
 reject=true;requests=[];await elements.connect.onclick();assert.equal(requests.length,1);assert.equal(elements.result.textContent,'401 unauthorized');
 console.log('Update page: application entry, authenticated session, manifest install, credential isolation and failure handling passed');
})().catch(e=>{console.error(e);process.exitCode=1});
