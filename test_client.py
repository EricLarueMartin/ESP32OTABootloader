import http.server
import tempfile,threading,urllib.request,urllib.error,unittest,hashlib
from pathlib import Path
from http.server import ThreadingHTTPServer
from client import handler,request,image_headers
class Ranges(unittest.TestCase):
 def test_release_manifest_binds_exact_image(self):
  import json
  with tempfile.TemporaryDirectory() as folder:
   image=Path(folder)/'app.bin';image.write_bytes(b'firmware')
   manifest=image.with_suffix('.json')
   value={'name':'test-app','version':'1.2.3','size':8,'sha256':hashlib.sha256(b'firmware').hexdigest()}
   manifest.write_text(json.dumps(value));self.assertEqual(image_headers(image,manifest)['X-Image-Version'],'1.2.3')
   image.write_bytes(b'changed!')
   with self.assertRaises(ValueError):image_headers(image,manifest)
   image.write_bytes(b'firmware');value['version']='bad\nheader';manifest.write_text(json.dumps(value))
   with self.assertRaises(ValueError):image_headers(image,manifest)
 def test_auth_redirect_is_refused(self):
  class Redirect(http.server.BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def do_GET(self):
    self.send_response(302);self.send_header('Location','http://127.0.0.1:1/leak');self.end_headers()
  server=ThreadingHTTPServer(('127.0.0.1',0),Redirect)
  threading.Thread(target=server.serve_forever,daemon=True).start()
  try:
   with self.assertRaisesRegex(RuntimeError,'redirect refused'):
    request({'address':'127.0.0.1:'+str(server.server_port),'key':'0'*64},'/v1/status')
  finally:server.shutdown();server.server_close()
 def test_exact_file_and_ranges(self):
  with tempfile.TemporaryDirectory() as d:
   image=Path(d)/'firmware.bin';data=bytes(range(256))*35;image.write_bytes(data);sha=hashlib.sha256(data).hexdigest()
   s=ThreadingHTTPServer(('127.0.0.1',0),handler(image,sha));t=threading.Thread(target=s.serve_forever,daemon=True);t.start()
   try:
    base='http://127.0.0.1:'+str(s.server_port);path='/image/'+sha+'.bin'
    image.write_bytes(b'changed on disk')  # handler must keep serving the original exact snapshot
    for start,end in [(0,4095),(4096,8191),(8192,len(data)-1)]:
     with urllib.request.urlopen(urllib.request.Request(base+path,headers={'Range':f'bytes={start}-{end}'})) as r:
      self.assertEqual(r.status,206);self.assertEqual(r.headers['Content-Range'],f'bytes {start}-{end}/{len(data)}');self.assertEqual(r.read(),data[start:end+1])
    for route,header,status in [('/../private',{'Range':'bytes=0-1'},404),(path,{},416),(path,{'Range':'bytes=0-4096'},416),(path,{'Range':'bytes=9000-9001'},416),(path,{'Range':'bytes=8-1'},416)]:
     with self.assertRaises(urllib.error.HTTPError) as e:urllib.request.urlopen(urllib.request.Request(base+route,headers=header))
     self.assertEqual(e.exception.code,status)
   finally:s.shutdown();s.server_close()
if __name__=='__main__':unittest.main()
