"""BTE1 complete-line parser for host adapters. No serial ownership."""
import struct

def parse_event(line):
    if isinstance(line,bytes):line=line.decode('ascii')
    line=line.rstrip('\r\n')
    p=line.split(',')
    if len(p)!=4 or p[0]!='@BTE1' or p[1]!='28' or len(p[2])!=56 or len(p[3])!=4:
        raise ValueError('bad BTE1 envelope')
    b=bytes.fromhex(p[2])
    if len(b)!=28:raise ValueError('bad BTE1 body')
    crc=0xffff
    for value in b:
        crc^=value
        for _ in range(8):crc=(crc>>1)^0xa001 if crc&1 else crc>>1
    if crc!=int(p[3],16):raise ValueError('bad BTE1 CRC')
    version,event,flags,reserved,session,event_id,beat_us,detected_us=struct.unpack('<BBBBIIQQ',b)
    if version!=1 or event!=1 or reserved!=0 or flags&~1:raise ValueError('unsupported BTE1 fields')
    return dict(version=version,event=event,flags=flags,session=session,event_id=event_id,
                beat_time_us=beat_us,detected_time_us=detected_us,clock='device_monotonic_us',
                beat_time_estimated=bool(flags&1))
