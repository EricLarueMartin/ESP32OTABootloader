"""Authenticated recovery API client and single-file HTTP range source.
Key JSON is private runtime data: {address,key}; never print contents.
No device operation occurs on import. Uncertain mutations are not retried.
"""
import argparse,hashlib,http.server,json,re,socket,threading,time,urllib.request
from pathlib import Path

class NoRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,req,fp,code,msg,headers,newurl):
  raise RuntimeError("Device redirect refused; credentials stay at the selected address")

def request(cfg,path,body=None,headers=None):
 if not re.fullmatch(r'[A-Za-z0-9.-]+(?::[0-9]+)?',cfg['address']):raise ValueError('Invalid device address')
 h={'Authorization':'Bearer '+cfg['key']};h.update(headers or {})
 req=urllib.request.Request('http://'+cfg['address']+path,data=body,headers=h)
 opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
 with opener.open(req,timeout=12) as r:return r.read()

def handler(image,digest):
 payload=image.read_bytes()  # immutable source snapshot; no per-range filesystem I/O
 class Handler(http.server.BaseHTTPRequestHandler):
  def log_message(self,*args):pass
  def do_GET(self):
   if self.path!='/image/'+digest+'.bin':self.send_error(404);return
   match=re.fullmatch(r'bytes=(\d+)-(\d+)',self.headers.get('Range',''))
   if not match:self.send_error(416);return
   first,last=map(int,match.groups());size=len(payload)
   if first>last or last>=size or last-first+1>4096:self.send_error(416);return
   data=payload[first:last+1]
   self.send_response(206);self.send_header('Content-Length',str(len(data)));self.send_header('Content-Range',f'bytes {first}-{last}/{size}');self.end_headers();self.wfile.write(data)
 return Handler

def image_headers(image, manifest_path=None):
 digest=hashlib.sha256(image.read_bytes()).hexdigest();size=image.stat().st_size
 headers={'X-Image-SHA256':digest,'X-Image-Size':str(size)}
 if manifest_path is not None:
  manifest=json.loads(manifest_path.read_text())
  if manifest.get('sha256')!=digest or manifest.get('size')!=size:raise ValueError('Manifest does not match image')
  for key,header in [('name','X-Image-Name'),('version','X-Image-Version')]:
   value=manifest.get(key,'')
   if not isinstance(value,str) or not re.fullmatch(r'[A-Za-z0-9._+-]{1,63}',value):raise ValueError('Invalid image label')
   headers[header]=value
 return headers

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--key-file',type=Path,required=True);ap.add_argument('action',choices=['status','download','launch','restart','hold','abandon']);ap.add_argument('--image',type=Path);ap.add_argument('--manifest',type=Path);ap.add_argument('--bind');ap.add_argument('--port',type=int,default=49195);a=ap.parse_args();cfg=json.loads(a.key_file.read_text())
 if a.action=='status':print(request(cfg,'/v1/status').decode());return
 if a.action!='download':print(request(cfg,'/v1/'+a.action,b'').decode());return
 if not a.image or not a.bind:raise SystemExit('download requires exact image and local LAN bind address')
 image=a.image.resolve();manifest=image_headers(image,a.manifest);digest=manifest['X-Image-SHA256'];size=int(manifest['X-Image-Size'])
 status=json.loads(request(cfg,'/v1/status'));assert status['role']=='recovery' and status['storage_ok'] and size<=status['capacity']
 # Serving only this exact image prevents directory/credential disclosure.
 server=http.server.ThreadingHTTPServer((a.bind,a.port),handler(image,digest));thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 try:
  print(request(cfg,'/v1/begin',b'',manifest).decode())
  source=f'http://{a.bind}:{a.port}/image/{digest}.bin'
  print(request(cfg,'/v1/download',source.encode(),{'Content-Type':'text/plain'}).decode())
  end=time.monotonic()+600;last=-1
  while time.monotonic()<end:
   time.sleep(1);status=json.loads(request(cfg,'/v1/status'))
   if status['committed']!=last:print('committed',status['committed'],'of',status['size']);last=status['committed']
   if not status['downloading']:
    if status['phase']!=2:raise RuntimeError('Stopped without Ready; read status before a new action')
    print('Verified image ready; launch is a separate explicit action');return
  raise TimeoutError('No completion; read status before a new action')
 finally:server.shutdown();server.server_close()
if __name__=='__main__':main()
