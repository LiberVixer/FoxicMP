"""Independent tag checksum oracle, separate from the production C++ loop."""
from progressive_mp3_fixture import frames

TABLE=[]
for i in range(256):
    value=i
    for _ in range(8):value=(value>>1)^(0xa001 if value&1 else 0)
    TABLE.append(value)

def crc(data, initial=0):
    value=initial
    for byte in data:value=(value>>8)^TABLE[(value^byte)&255]
    return value

def metadata(raw):
    at,n=frames(raw)[0];header=raw[at:at+n];channels=1 if header[3]>>6==3 else 2
    start=4+(17 if channels==1 else 32);kind=header[start:start+4]
    if kind not in (b'Xing',b'Info'):return header,None
    flags=int.from_bytes(header[start+4:start+8],'big')
    return header,start+16+(100 if flags&4 else 0)+(4 if flags&8 else 0)
