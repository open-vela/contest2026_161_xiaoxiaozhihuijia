#!/usr/bin/env python3
"""USB serial IMU + front camera recorder for BLE-independent baseline."""
import argparse, csv, json, struct, time, zlib
from pathlib import Path

def crc16(b):
    c=0xffff
    for x in b:
        c ^= x
        for _ in range(8): c=(c>>1)^0xa001 if c&1 else c>>1
    return c

def parse(line):
    if not line.startswith('@IMU1,'): return None
    p=line.strip().split(',')
    if len(p)!=4 or int(p[1])!=27 or len(p[2])!=54: raise ValueError('bad frame boundary/length')
    body=bytes.fromhex(p[2]); got=int(p[3],16)
    if crc16(body)!=got: raise ValueError('crc mismatch')
    if body[0]!=2 or body[1]!=2: raise ValueError('unsupported frame')
    sid,seq,uptime=struct.unpack_from('<III',body,2)
    ax,ay,az,gx,gy,gz=struct.unpack_from('<hhhhhh',body,14)
    return sid,seq,uptime,ax,ay,az,gx,gy,gz,body[26]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--serial-port',default='COM7'); ap.add_argument('--baud',type=int,default=1000000); ap.add_argument('--duration-s',type=float,default=30); ap.add_argument('--camera-index',type=int,default=0); ap.add_argument('--output-dir',type=Path,required=True)
    a=ap.parse_args(); a.output_dir.mkdir(parents=False,exist_ok=False)
    import serial, cv2
    ser=serial.Serial(a.serial_port,a.baud,timeout=.05); cam=cv2.VideoCapture(a.camera_index,cv2.CAP_DSHOW)
    if not cam.isOpened(): raise RuntimeError('camera open failed')
    fps=cam.get(cv2.CAP_PROP_FPS) or 30; w=int(cam.get(cv2.CAP_PROP_FRAME_WIDTH)); h=int(cam.get(cv2.CAP_PROP_FRAME_HEIGHT)); out=cv2.VideoWriter(str(a.output_dir/'video.mp4'),cv2.VideoWriter_fourcc(*'mp4v'),fps,(w,h))
    raw=(a.output_dir/'serial_raw.bin').open('wb'); err=(a.output_dir/'parse_errors.log').open('w'); csvf=(a.output_dir/'raw_accel.csv').open('w',newline=''); wr=csv.writer(csvf); wr.writerow(['received_at_local','received_epoch_us','record_type','device_sequence','expected_sequence','missing_count','device_uptime_ms','raw_x','raw_y','raw_z','x_mg','y_mg','z_mg','loss_reason','last_sample','serial_line'])
    vf=(a.output_dir/'video_frames.csv').open('w',newline=''); vw=csv.writer(vf); vw.writerow(['frame_index','host_monotonic_ns','host_epoch_us'])
    start=time.monotonic(); buf=b''; expected=None; fi=0; last=None; count=0
    try:
      while time.monotonic()-start<a.duration_s:
        chunk=ser.read(ser.in_waiting or 1); raw.write(chunk); buf+=chunk
        while b'\n' in buf:
          line,buf=buf.split(b'\n',1)
          try:
            v=parse(line.decode(errors='replace'))
            if v:
              sid,seq,up,ax,ay,az,gx,gy,gz,flags=v; miss=0 if expected is None else max(0,seq-expected); wr.writerow([time.strftime('%Y-%m-%dT%H:%M:%S%z'),time.time_ns()//1000,'sample',seq,seq if expected is None else expected,miss,up,ax,ay,az,ax*.122,ay*.122,az*.122,'',1,line.decode(errors='replace')]); expected=seq+1; last=seq; count+=1
              with (a.output_dir/'raw_imu.csv').open('a', newline='') as imu:
                if imu.tell()==0: csv.writer(imu).writerow(['sequence','device_uptime_ms','raw_ax','raw_ay','raw_az','raw_gx','raw_gy','raw_gz','flags'])
                csv.writer(imu).writerow([seq,up,ax,ay,az,gx,gy,gz,flags])
          except Exception as e: err.write(repr(e)+' '+repr(line)+'\n')
        ok,frame=cam.read()
        if ok: out.write(frame); vw.writerow([fi,time.monotonic_ns(),time.time_ns()//1000]); fi+=1
    finally:
      ser.close(); cam.release(); out.release(); raw.close(); err.close(); csvf.close(); vf.close()
      (a.output_dir/'session_metadata.json').write_text(json.dumps({'serial_port':a.serial_port,'baud':a.baud,'duration_s':a.duration_s,'sample_count':count,'last_sequence':last,'note':'video and IMU are not hardware synchronized; hold still 2s then make three downbeats'},indent=2))
if __name__=='__main__': main()
