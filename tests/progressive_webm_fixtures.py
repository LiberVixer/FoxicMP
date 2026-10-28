"""Small EBML helpers for fixture boundaries/mutations, never for production parsing."""
def width(first,maximum=8):
    for n in range(1,maximum+1):
        if first&(0x80>>(n-1)):return n
    raise ValueError('Bad EBML integer')

def element(data,at):
    ids=width(data[at],4);sizes=width(data[at+ids]);head=at+ids+sizes
    tag=int.from_bytes(data[at:at+ids],'big');size=data[at+ids]&((0x80>>(sizes-1))-1)
    for byte in data[at+ids+1:head]:size=(size<<8)|byte
    return tag,head,None if size==(1<<(7*sizes))-1 else head+size

def webm_prefix(data,count):
    tag,_,at=element(data,0);assert tag==0x1a45dfa3
    tag,at,limit=element(data,at);assert tag==0x18538067
    clusters=0
    while at<(limit or len(data)):
        tag,_,end=element(data,at)
        if end is None or end>len(data):raise ValueError('Incomplete fixture cluster')
        if tag==0x1f43b675:
            clusters+=1
            if clusters==count:return end
        at=end
    raise ValueError('Insufficient fixture clusters')

def locate(data,tag):
    masters={0x1a45dfa3,0x18538067,0x1549a966,0x1654ae6b,0xae,0xe0,0xe1,0x1f43b675,0xa0}
    def walk(start,end):
        at=start
        while at<end:
            kind,body,stop=element(data,at);stop=stop or end
            if kind==tag:return at,body,stop
            if kind in masters:
                found=walk(body,stop)
                if found:return found
            at=stop
    found=walk(0,len(data))
    if found is None:raise ValueError('Missing EBML field '+hex(tag))
    return found
