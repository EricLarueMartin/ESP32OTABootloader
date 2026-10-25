"""Bench-only early-crash and watchdog tests with UART evidence; restores good app."""
import argparse,json,socket,threading,time
from pathlib import Path
from http.server import ThreadingHTTPServer
import serial
from client import request,handler,image_headers
P=Path(__file__).resolve().parent

def main():
    ap=argparse.ArgumentParser();ap.add_argument("--key-file",type=Path,required=True)
    ap.add_argument("--port",required=True);ap.add_argument("--output",type=Path,required=True)
    ap.add_argument("--bench-no-actuators",action="store_true",required=True);a=ap.parse_args()
    cfg=json.loads(a.key_file.read_text())
    if cfg.get("netid")!="huzzah32-test":raise RuntimeError("Dedicated bench identity required")
    a.output.mkdir(parents=True,exist_ok=False)
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.connect((cfg["address"],80));bind=sock.getsockname()[0];sock.close()
    def state():return json.loads(request(cfg,"/v1/status"))
    def wait(predicate,seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            try:
                current=state()
                if predicate(current):return current
            except (OSError,ValueError):pass
            time.sleep(.2)
        raise RuntimeError("Expected state absent; inspect before any further write")
    def install(profile):
        image=P/"build/huzzah32"/profile/"binary/selftest.ino.bin"
        headers=image_headers(image,image.with_suffix(".manifest.json"));digest=headers["X-Image-SHA256"]
        current=state()
        if current.get("role")!="recovery" or current.get("netid")!="huzzah32-test":raise RuntimeError("Recovery identity required")
        server=ThreadingHTTPServer((bind,49197),handler(image,digest));threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            request(cfg,"/v1/begin",b"",headers)
            source=f"http://{bind}:49197/image/{digest}.bin".encode()
            request(cfg,"/v1/download",source,{"Content-Type":"text/plain"})
            end=time.monotonic()+600
            while time.monotonic()<end:
                time.sleep(.5);current=state()
                if not current["downloading"]:
                    if current["phase"]!=2:raise RuntimeError("Transfer stopped: "+current["result"]+"; checkpoint="+str(current["committed"]))
                    print("Ready:",profile,flush=True);return digest
            raise TimeoutError("Download did not finish")
        finally:server.shutdown();server.server_close()
    # Set modem lines before opening; don't deliberately reset the test device.
    conn=serial.Serial();conn.port=a.port;conn.baudrate=115200;conn.timeout=.05;conn.dtr=False;conn.rts=False;conn.open()
    captured=bytearray();stop=threading.Event()
    def collect():
        while not stop.is_set():
            captured.extend(conn.read(4096))
    thread=threading.Thread(target=collect,daemon=True);thread.start()
    try:
        time.sleep(2)
        for profile,phase in [("badboot",3),("badloop",4)]:
            digest=install(profile);start=len(captured)
            request(cfg,"/v1/launch",b"");time.sleep(2)
            returned=wait(lambda v:v.get("role")=="recovery" and v.get("phase")==phase and v.get("image_sha256")==digest)
            time.sleep(12);stable=state()
            if stable.get("role")!="recovery" or stable.get("image_sha256")!=digest:raise RuntimeError("Recovery did not remain stable")
            if returned.get("boot_window_mode")!=0 or stable.get("boot_window_mode")!=0:raise RuntimeError("Fault recovery must have no automatic launch window")
            uart=bytes(captured[start:]).decode(errors="replace")
            (a.output/(profile+"-uart.log")).write_text(uart)
            if "rst:" not in uart:raise RuntimeError("Missing UART reset evidence")
            if profile=="badloop" and not any(word in uart.lower() for word in ("watchdog","wdt")):
                raise RuntimeError("Missing watchdog evidence")
            (a.output/(profile+".json")).write_text(json.dumps(dict(returned=returned,stable=stable,uart_reset_observed=True),indent=2))
            print("Recovered and stable:",profile,flush=True)
        install("selftest");request(cfg,"/v1/launch",b"");time.sleep(3)
        normal=wait(lambda v:v.get("role")=="main_selftest")
        (a.output/"restored.json").write_text(json.dumps(normal,indent=2))
        print("Good selftest restored and running",flush=True)
    finally:
        stop.set();thread.join(timeout=2);conn.close()
if __name__=="__main__":main()
