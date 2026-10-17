#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create locally named Tahoma OS faces; preserve original glyphs and notices."""
import argparse, hashlib, json, struct
from pathlib import Path

def tables(data):
 if data[:4] != b'\x00\x01\x00\x00':raise ValueError('Expected a TrueType SFNT font')
 count=struct.unpack_from('>H',data,4)[0]
 result={}
 for i in range(count):
  tag,checksum,offset,length=struct.unpack_from('>4sIII',data,12+i*16)
  if offset+length>len(data):raise ValueError('Truncated font table')
  result[tag]=data[offset:offset+length]
 return result

def checksum(data):
 data+=b'\0'*((-len(data))%4)
 return sum(struct.unpack('>'+str(len(data)//4)+'I',data))&0xffffffff

def renamed(data):
 original=tables(data);items=dict(original);name=items[b'name']
 form,count,offset=struct.unpack_from('>HHH',name)
 if form!=0:raise ValueError('Expected the format-0 name table used by XP Tahoma')
 records=[];strings=bytearray();found=False
 for i in range(count):
  platform,encoding,language,kind,length,start=struct.unpack_from('>6H',name,6+12*i)
  value=name[offset+start:offset+start+length]
  if len(value)!=length:raise ValueError('Truncated name string')
  codec='utf-16-be' if platform in (0,3) else 'mac_roman' if platform==1 else None
  if codec and kind in (1,3,4,6,16,18,21):
   text=value.decode(codec)
   if 'Tahoma' in text:
    text=text.replace('Tahoma','TahomaOS' if kind==6 else 'Tahoma OS');value=text.encode(codec);found=True
  records.append(struct.pack('>6H',platform,encoding,language,kind,len(value),len(strings)));strings.extend(value)
 if not found:raise ValueError('The font is not named Tahoma')
 items[b'name']=struct.pack('>HHH',0,count,6+12*count)+b''.join(records)+strings
 # Naming invalidates digital signatures; no signature is carried to a renamed face.
 items.pop(b'DSIG',None)
 head=bytearray(items[b'head']);head[8:12]=b'\0'*4;items[b'head']=bytes(head)
 tags=sorted(items);count=len(tags);power=2**(count.bit_length()-1)
 directory=bytearray(struct.pack('>4s4H',data[:4],count,power*16,power.bit_length()-1,count*16-power*16))
 body=bytearray();offset=12+16*count;head_offset=None
 for tag in tags:
  table=items[tag]
  directory.extend(struct.pack('>4sIII',tag,checksum(table),offset,len(table)))
  if tag==b'head':head_offset=offset
  padded=table+b'\0'*((-len(table))%4);body.extend(padded);offset+=len(padded)
 result=directory+body
 struct.pack_into('>I',result,head_offset+8,(0xB1B0AFBA-checksum(bytes(result)))&0xffffffff)
 return bytes(result)

def generate(source,destination):
 destination.mkdir(parents=True,exist_ok=True);manifest={}
 for filename,output in [('tahoma.ttf','tahoma-os.ttf'),('tahomabd.ttf','tahoma-os-bold.ttf')]:
  file=source/filename
  if not file.is_file():raise FileNotFoundError('Missing original Tahoma face: '+filename)
  original=file.read_bytes();result=renamed(original)
  target=destination/output
  if not target.is_file() or target.read_bytes()!=result:
   temporary=target.with_suffix('.tmp');temporary.write_bytes(result);temporary.replace(target)
  manifest[output]={'source_sha256':hashlib.sha256(original).hexdigest(),'sha256':hashlib.sha256(result).hexdigest()}
 (destination/'PROVENANCE.json').write_text(json.dumps({'family':'Tahoma OS','copyright':'Original Microsoft copyright and naming records retained except family/face identifiers.','faces':manifest},indent=2)+'\n')
 return manifest
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path)
 args=parser.parse_args();generate(args.source,args.destination)
