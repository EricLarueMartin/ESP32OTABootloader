"""Native fault checks and partition bounds; never contacts hardware."""
import os,subprocess,json,hashlib
from pathlib import Path
p=Path(__file__).resolve().parent;out=p/'build/tests';out.mkdir(parents=True,exist_ok=True)
ms=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.42.34433');sdk=Path('C:/Program Files (x86)/Windows Kits/10');env=dict(os.environ)
env['INCLUDE']=';'.join(str(x) for x in [ms/'include',sdk/'Include/10.0.22621.0/ucrt',sdk/'Include/10.0.22621.0/shared',sdk/'Include/10.0.22621.0/um']);env['LIB']=';'.join(str(x) for x in [ms/'lib/x64',sdk/'Lib/10.0.22621.0/ucrt/x64',sdk/'Lib/10.0.22621.0/um/x64']);env['PATH']=str(ms/'bin/Hostx64/x64')+';'+env['PATH']
r=subprocess.run([str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/EHsc','/std:c++17','/W4','/WX','/O2','/I'+str(p),str(p/'test_state.cpp'),'/Fe:'+str(out/'test_state.exe'),'/Fo:'+str(out/'test_state.obj')],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'test-compile.log').write_bytes(r.stdout)
if r.returncode:print(r.stdout.decode(errors='replace'));raise SystemExit(r.returncode)
r=subprocess.run([str(out/'test_state.exe')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);assert r.returncode==0,r.stdout
parts=[]
for line in (p/'recovery/partitions.csv').read_text().splitlines():
 if not line or line.startswith('#'):continue
 f=line.split(',');parts.append((f[0],int(f[3],0),int(f[4],0)))
for i,(name,start,size) in enumerate(parts):
 assert start%4096==0 and size%4096==0 and start+size<=0x400000
 assert all(start+size<=s or s+n<=start for _,s,n in parts[:i])
assert dict((name,(start,size)) for name,start,size in parts)['nvs']==(0x9000,0x5000)
result={'checks':r.stdout.decode().strip(),'partition_bounds_and_no_overlap':True,'existing_nvs_preserved':True,'hardware_qualified':False,'source_sha256':{x:hashlib.sha256((p/x).read_bytes()).hexdigest() for x in ['test_state.cpp','recovery/update_state.h','recovery/identity.h','recovery/readback.h','recovery/http_range.h','recovery/partitions.csv']}}
(out/'native-validation.json').write_text(json.dumps(result,indent=2)+'\n');print(result['checks']);print('Partition boundaries and existing NVS address validated')
