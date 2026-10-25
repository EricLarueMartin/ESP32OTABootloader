"""Bench-only failed-update qualification using a no-actuator test image.
Run once; deliberately interrupts a main-image download and rejects corrupt data.
Never touches loader, partition table, default NVS, target MCU or motors.
"""
import argparse,hashlib,json,socket,threading,time,urllib.error
from pathlib import Path
from http.server import ThreadingHTTPServer
from client import request,handler

def main():
 ap=argparse.ArgumentParser()
 ap.add_argument('--key-file',type=Path,required=True)
 ap.add_argument('--image',type=Path,required=True)
 ap.add_argument('--output',type=Path,required=True)
 ap.add_argument('--bench-no-actuators',action='store_true',required=True)
 a=ap.parse_args();folder=a.output;folder.mkdir(parents=True,exist_ok=False)
 cfg=json.loads(a.key_file.read_text());image=a.image.resolve();sha=hashlib.sha256(image.read_bytes()).hexdigest();size=image.stat().st_size
 if cfg.get('netid')!='huzzah32-test':raise RuntimeError('Only the dedicated test device is allowed')
 sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.connect((cfg['address'],80));bind=sock.getsockname()[0];sock.close()
 def state():return json.loads(request(cfg,'/v1/status'))
 def wait(predicate,seconds=600):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   try:
    s=state()
    if predicate(s):return s
    if s.get('phase')==1 and not s.get('downloading') and s.get('result') not in ('idle','downloading'):raise RuntimeError('Download stopped: '+str(s))
   except (OSError,ValueError):pass
   time.sleep(.3)
  raise RuntimeError('Expected read-only state not observed; no mutation retry')
 def begin():return request(cfg,'/v1/begin',b'',{'X-Image-SHA256':sha,'X-Image-Size':str(size),'X-Image-Name':'esp32-ota-selftest','X-Image-Version':'qualification-'+sha[:12]}).decode()
 ranges=[]
 def serve(file,stop_after=None):
  base=handler(file,sha)
  class H(base):
   def do_GET(self):
    ranges.append(self.headers.get('Range'))
    if stop_after is not None and int(self.headers.get('Range','bytes=0-0').split('=')[1].split('-')[0])>=stop_after:self.send_error(503);return
    super().do_GET()
  server=ThreadingHTTPServer((bind,49196),H);threading.Thread(target=server.serve_forever,daemon=True).start();return server
 def start():return request(cfg,'/v1/download',f'http://{bind}:49196/image/{sha}.bin'.encode(),{'Content-Type':'text/plain'}).decode()
 wrong=dict(cfg,key='0'*64 if cfg['key']!='0'*64 else '1'*64)
 try:request(wrong,'/v1/status');raise AssertionError('Wrong password accepted')
 except urllib.error.HTTPError as e:assert e.code==401
 before=state();assert before['role']=='recovery' and before['netid']==cfg['netid'] and before['phase'] in (0,2,4,5)
 server=serve(image,16384)
 try:
  begin();start();stopped=wait(lambda x:not x['downloading'] and x['phase']==1);assert stopped['committed']==16384
 finally:server.shutdown();server.server_close()
 (folder/'interrupted.json').write_text(json.dumps({'state':stopped,'ranges':ranges},indent=2)+'\n')
 request(cfg,'/v1/restart',b'');time.sleep(2);rebooted=wait(lambda x:x['role']=='recovery' and x['phase']==1);assert rebooted['committed']==16384
 ranges.clear();server=serve(image)
 try:
  assert begin()=='resume_existing';start();ready=wait(lambda x:not x['downloading'] and x['phase']==2);assert ranges[0]=='bytes=16384-20479';assert ready['release']=={'name':'esp32-ota-selftest','version':'qualification-'+sha[:12]};assert ready['image_sha256']==sha
 finally:server.shutdown();server.server_close()
 (folder/'resume-verified.json').write_text(json.dumps({'after_restart':rebooted,'after_resume':ready,'first_range':ranges[0],'no_physical_power_cycle':True},indent=2)+'\n')
 bad=folder/'corrupt-selftest.bin';data=bytearray(image.read_bytes());data[-1]^=1;bad.write_bytes(data);ranges.clear();server=serve(bad)
 try:
  begin();start();rejected=wait(lambda x:not x['downloading'] and x['committed']==size);assert rejected['phase']==1 and rejected['result']=='image_verification_failed'
 finally:server.shutdown();server.server_close()
 try:request(cfg,'/v1/launch',b'');raise AssertionError('Corrupt launch was accepted')
 except urllib.error.HTTPError as e:assert e.code==409
 request(cfg,'/v1/abandon',b'');final=state();assert final['phase']==0 and final['storage_ok']
 (folder/'corrupt-rejected.json').write_text(json.dumps({'rejected':rejected,'launch_refused_409':True,'final':final,'loader_still_authenticated':True},indent=2)+'\n')
 # Restore the good image, launch it, then prove authenticated software return.
 ranges.clear();server=serve(image)
 try:
  begin();start();wait(lambda x:not x['downloading'] and x['phase']==2)
 finally:server.shutdown();server.server_close()
 request(cfg,'/v1/launch',b'');time.sleep(3)
 wait(lambda x:x['role']=='main_selftest')
 request(cfg,'/v1/recover',b'');time.sleep(3)
 restored=wait(lambda x:x['role']=='recovery' and x['phase']==4)
 (folder/'restored.json').write_text(json.dumps(restored,indent=2)+'\n')
 print('Interrupted download checkpoint survived software restart; resumed at exact offset. Corrupt image and launch rejected; authenticated recovery retained.')
if __name__=='__main__':main()
