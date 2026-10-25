"""HUZZAH32-only EN-reset gesture checks; physical USB power removal is separate."""
import argparse,json,threading,time,urllib.error,urllib.request
from pathlib import Path
import serial
from client import request

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--key-file',type=Path,required=True)
    ap.add_argument('--port',required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--bench-no-actuators',action='store_true',required=True);a=ap.parse_args()
    cfg=json.loads(a.key_file.read_text());assert cfg['netid']=='huzzah32-test'
    a.output.mkdir(parents=True,exist_ok=False)
    def state():return json.loads(request(cfg,'/v1/status'))
    def wait(predicate,seconds=50):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            try:
                s=state()
                if predicate(s):return s
            except (OSError,ValueError):pass
            time.sleep(.2)
        raise RuntimeError('Expected state absent; inspect before another operation')
    initial=state();assert initial['role']=='main_selftest' and initial['no_actuators']
    conn=serial.Serial();conn.port=a.port;conn.baudrate=115200;conn.timeout=.05;conn.dtr=False;conn.rts=False;conn.open()
    captured=bytearray();stop=threading.Event()
    def collect():
        while not stop.is_set():captured.extend(conn.read(4096))
    thread=threading.Thread(target=collect,daemon=True);thread.start()
    def reset():
        conn.rts=True;time.sleep(.15);conn.rts=False
    results={'method':'CP2104 RTS/EN resets, not physical power removal','physical_double_power_cycle_verified':False,'hardware_full_15_minute_soak':False}
    try:
        reset()
        first=wait(lambda s:s.get('boot_window_mode')==1)
        assert first['boot_window_ms']>2500
        results['first_startup']=first
        reset()
        emergency=wait(lambda s:s.get('boot_window_mode')==2)
        assert emergency['boot_window_ms']>850000
        results['emergency_entry']=emergency
        time.sleep(11)
        later=state();assert later['boot_window_mode']==2 and 0<later['boot_window_ms']<emergency['boot_window_ms']
        results['emergency_countdown']=later
        wrong=dict(cfg,key='0'*64 if cfg['key']!='0'*64 else '1'*64)
        try:request(wrong,'/v1/maintenance',b'');raise AssertionError('Unauthenticated maintenance accepted')
        except urllib.error.HTTPError as e:assert e.code==401
        assert state()['boot_window_mode']==2
        results['wrong_password_rejected']=True
        request(cfg,'/v1/maintenance',b'')
        held=state();assert held['phase']==5 and held['boot_window_mode']==0
        reset();held_after_reset=wait(lambda s:s.get('role')=='recovery' and s.get('phase')==5)
        time.sleep(11);assert state()['boot_window_mode']==0
        results['authenticated_hold_survives_reset']=held_after_reset
        request(cfg,'/v1/launch',b'');wait(lambda s:s.get('role')=='main_selftest')
        # One normal cold reset must still auto-launch the healthy image.
        reset();normal=wait(lambda s:s.get('boot_window_mode')==1)
        wait(lambda s:s.get('role')=='main_selftest')
        results['normal_cold_start_auto_launch']=True
        request(cfg,'/v1/recover',b'');software=wait(lambda s:s.get('role')=='recovery')
        assert software['boot_window_mode']==0
        results['software_entry_indefinite']=software
        request(cfg,'/v1/launch',b'');results['restored']=wait(lambda s:s.get('role')=='main_selftest')
        print('EN double-reset emergency window, authenticated persistent hold, normal startup and software handoff passed.',flush=True)
    finally:
        stop.set();thread.join(timeout=2);conn.close()
        (a.output/'result.json').write_text(json.dumps(results,indent=2)+'\n')
        (a.output/'uart.log').write_text(captured.decode(errors='replace'),encoding='utf-8')
if __name__=='__main__':main()
