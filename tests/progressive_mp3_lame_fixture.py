"""Real libmp3lame C API fixture, including the encoder's own final Info tag."""
import ctypes
import ctypes.util
import os
from pathlib import Path
import subprocess


def encode(root, rate, channels, seconds=4):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    library=ctypes.util.find_library('mp3lame')
    cookie=None
    if os.name=='nt':
        # shutil.which() applies PATHEXT/execute rules; a DLL is a library,
        # so locate the exact file in PATH rather than treating it as a CLI.
        path=next((str(Path(folder)/'libmp3lame-0.dll')
                   for folder in os.environ.get('PATH','').split(os.pathsep)
                   if folder and (Path(folder)/'libmp3lame-0.dll').is_file()),None)
        if not path:raise RuntimeError('libmp3lame-0.dll is required in the test PATH')
        cookie=os.add_dll_directory(str(Path(path).parent));library=path
    if not library:raise RuntimeError('libmp3lame is required for its independent encoder tag')
    lib=ctypes.CDLL(library)
    ptr=ctypes.c_void_p;short=ctypes.POINTER(ctypes.c_short);byte=ctypes.POINTER(ctypes.c_ubyte)
    def function(name,args,result=ctypes.c_int):
        f=getattr(lib,name);f.argtypes=args;f.restype=result;return f
    init=function('lame_init',[],ptr);close=function('lame_close',[ptr])
    initialize=function('lame_init_params',[ptr])
    encode_stereo=function('lame_encode_buffer_interleaved',[ptr,short,ctypes.c_int,byte,ctypes.c_int])
    encode_mono=function('lame_encode_buffer',[ptr,short,short,ctypes.c_int,byte,ctypes.c_int])
    flush=function('lame_encode_flush',[ptr,byte,ctypes.c_int])
    tag=function('lame_get_lametag_frame',[ptr,byte,ctypes.c_size_t],ctypes.c_size_t)
    version=function('get_lame_version',[],ctypes.c_char_p)().decode('ascii')
    pcm=subprocess.check_output(['ffmpeg','-v','error','-f','lavfi','-i',
        'aevalsrc=0.2*sin(2*PI*(431+17*t)*t):s='+str(rate)+':d='+str(seconds),
        '-ac',str(channels),'-c:a','pcm_s16le','-f','s16le','-'])
    samples=(ctypes.c_short*(len(pcm)//2)).from_buffer_copy(pcm)
    count=len(pcm)//(channels*2);capacity=count*5//4+7200;buffer=(ctypes.c_ubyte*capacity)()
    handle=init()
    if not handle:raise RuntimeError('lame_init failed')
    try:
        for name,value in [('lame_set_in_samplerate',rate),('lame_set_out_samplerate',rate),
                           ('lame_set_num_channels',channels),('lame_set_brate',128),
                           ('lame_set_quality',2),('lame_set_bWriteVbrTag',1)]:
            if function(name,[ptr,ctypes.c_int])(handle,value)<0:raise RuntimeError(name)
        function('lame_set_write_id3tag_automatic',[ptr,ctypes.c_int],None)(handle,0)
        if initialize(handle)<0:raise RuntimeError('lame_init_params failed')
        n=encode_stereo(handle,samples,count,buffer,capacity) if channels==2 else encode_mono(handle,samples,samples,count,buffer,capacity)
        if n<0:raise RuntimeError('libmp3lame encode failed')
        body=bytes(buffer[:n]);n=flush(handle,buffer,capacity)
        if n<0:raise RuntimeError('libmp3lame flush failed')
        body+=bytes(buffer[:n]);n=tag(handle,buffer,capacity)
        if not n or n>capacity:raise RuntimeError('libmp3lame did not supply a final tag')
        body=bytes(buffer[:n])+body[n:]
        output=root/'lame.mp3';output.write_bytes(body)
        return output,body,version
    finally:
        close(handle)
        if cookie:cookie.close()
