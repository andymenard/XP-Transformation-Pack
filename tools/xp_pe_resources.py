# SPDX-License-Identifier: GPL-3.0-or-later
import struct
from pathlib import Path

def resources(p):
 b=Path(p).read_bytes();pe=struct.unpack_from('<I',b,60)[0];count=struct.unpack_from('<H',b,pe+6)[0];optional_size=struct.unpack_from('<H',b,pe+20)[0];op=pe+24;sections=op+optional_size
 magic=struct.unpack_from('<H',b,op)[0];rva=struct.unpack_from('<I',b,op+(112 if magic==0x20b else 96)+16)[0]
 def offset(rva):
  for i in range(count):
   vs,va,size,raw=struct.unpack_from('<IIII',b,sections+40*i+8)
   if va<=rva<va+max(vs,size):return raw+rva-va
  raise ValueError(rva)
 root=offset(rva);out={}
 def walk(at,parts):
  n=sum(struct.unpack_from('<HH',b,root+at+12))
  for i in range(n):
   name,target=struct.unpack_from('<II',b,root+at+16+i*8)
   if name&0x80000000:
    pos=root+(name&0x7fffffff);length=struct.unpack_from('<H',b,pos)[0];name=b[pos+2:pos+2+length*2].decode('utf-16le')
   if target&0x80000000:walk(target&0x7fffffff,parts+(name,))
   else:
    loc,size=struct.unpack_from('<II',b,root+target);out[parts+(name,)]=b[offset(loc):offset(loc)+size]
 walk(0,());return out
