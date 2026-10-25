"""Explicit one-time HUZZAH32 bench layout installation; never ordinary OTA."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
P=Path(__file__).resolve().parent

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--port",required=True);ap.add_argument("--expected-mac",required=True)
    ap.add_argument("--backup",type=Path,required=True);ap.add_argument("--esptool",type=Path,required=True)
    ap.add_argument("--stable-usb-bench",action="store_true",required=True)
    a=ap.parse_args()
    if a.backup.stat().st_size!=0x400000:raise RuntimeError("Require completed private 4 MiB backup")
    build=P/"build/huzzah32/recovery";binary=build/"binary"
    receipt=json.loads((build/"receipt.json").read_text())
    image=binary/"recovery.ino.bin"
    if hashlib.sha256(image.read_bytes()).hexdigest()!=receipt["image_sha256"]:raise RuntimeError("Image receipt mismatch")
    for name,digest in receipt["sources"].items():
        current=P/"arduino_startup.h" if name=="arduino_startup.h" else P/"recovery"/name
        if hashlib.sha256(current.read_bytes()).hexdigest()!=digest:raise RuntimeError("Source changed: "+name)
    def run(args):
        r=subprocess.run([str(a.esptool),"--chip","esp32","--port",a.port]+args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if r.returncode:raise RuntimeError(r.stdout.decode(errors="replace")[-1500:])
        return r.stdout.decode(errors="replace")
    identity=run(["flash-id"])
    if a.expected_mac.lower() not in identity.lower() or "Detected flash size: 4MB" not in identity:raise RuntimeError("Device identity/capacity mismatch")
    # This profile is not qualified for secure-boot/encrypted-flash devices.
    efuse=a.esptool.with_name("espefuse"+a.esptool.suffix)
    security=subprocess.run([str(efuse),"--chip","esp32","--port",a.port,"summary","FLASH_CRYPT_CNT","ABS_DONE_0","ABS_DONE_1"],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    text=security.stdout.decode(errors="replace")
    if security.returncode or not re.search(r"(?m)^FLASH_CRYPT_CNT.*=\s*0\s",text) or any(not re.search(r"(?m)^"+field+r".*=\s*False\s",text) for field in ("ABS_DONE_0","ABS_DONE_1")):
        raise RuntimeError("Security state unsupported or unverified; no layout writes")
    blank8=build/"blank-8k.bin";blank8.write_bytes(b"\xff"*8192)
    blank64=build/"blank-64k.bin";blank64.write_bytes(b"\xff"*65536)
    writes=[(0x1000,binary/"recovery.ino.bootloader.bin"),(0x8000,binary/"recovery.ino.partitions.bin"),(0xe000,blank8),(0x10000,image),(0x3d0000,blank8),(0x3e0000,blank64)]
    limits={0x1000:0x8000,0x8000:0x9000,0xe000:0x10000,0x10000:0x110000,0x3d0000:0x3d2000,0x3e0000:0x3f0000}
    for offset,file in writes:
        end=(offset+file.stat().st_size+4095)&~4095
        if end>limits[offset]:raise RuntimeError("Image exceeds its allowed region")
        if not (end<=0x9000 or offset>=0xe000):raise RuntimeError("Would overwrite default NVS")
        if end>0x3f0000:raise RuntimeError("Would overwrite coredump/end of flash")
    intent=build/"usb-install-intent.json"
    with intent.open("x") as f:
        json.dump(dict(mac=a.expected_mac,backup_sha256=hashlib.sha256(a.backup.read_bytes()).hexdigest(),
            writes=[dict(offset=offset,size=file.stat().st_size,sha256=hashlib.sha256(file.read_bytes()).hexdigest()) for offset,file in writes]),f,indent=2)
    args=["--baud","460800","write-flash","--flash-mode","dio","--flash-freq","80m","--flash-size","4MB"]
    for offset,file in writes:args += [hex(offset),str(file)]
    try: result=run(args)
    except Exception:
        raise RuntimeError("Installation uncertain. Intent retained; inspect board before any further write.") from None
    (build/"usb-install.log").write_text(result)
    print("Bench layout written and ROM verification passed; provisioning/functional tests still required.")
if __name__=="__main__":main()
