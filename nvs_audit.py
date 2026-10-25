"""Read-only NVS audit helpers. Keep raw partitions and decoded values private."""
import struct,zlib

def parse(data):
    if len(data)%4096:raise ValueError("NVS size is not page aligned")
    pages=[];records={};names={0:"namespace"}
    for off in range(0,len(data),4096):
        page=data[off:off+4096]
        if page==b"\xff"*4096:continue
        state,seq=struct.unpack_from("<II",page)
        if state not in (0xfffffffe,0xfffffffc,0xfffffff8):raise ValueError("Unknown page state")
        if zlib.crc32(page[4:28],0xffffffff)&0xffffffff!=struct.unpack_from("<I",page,28)[0]:raise ValueError("Page CRC")
        pages.append((seq,page))
    for seq,page in sorted(pages):
        i=0
        while i<126:
            if ((page[32+i//4]>>((i%4)*2))&3)!=2:i+=1;continue
            item=page[64+i*32:96+i*32];ns,kind,span,chunk=item[:4]
            if not span or i+span>126:raise ValueError("Entry span")
            if zlib.crc32(item[:4]+item[8:],0xffffffff)&0xffffffff!=struct.unpack_from("<I",item,4)[0]:raise ValueError("Entry CRC")
            key=item[8:24].split(b"\0")[0].decode("ascii");value=item[24:]
            if kind in (0x21,0x41,0x42):
                size=struct.unpack_from("<H",value)[0];payload=page[64+(i+1)*32:64+(i+span)*32][:size]
                if len(payload)!=size or zlib.crc32(payload,0xffffffff)&0xffffffff!=struct.unpack_from("<I",value,4)[0]:raise ValueError("Payload CRC")
                value=payload
            records[(ns,key,kind,chunk)]=value
            if ns==0:names[value[0]]=key
            i+=span
    return {(names.get(ns,str(ns)),key,kind,chunk):value for (ns,key,kind,chunk),value in records.items()}

def logical(records):
    result={}
    for (namespace,key,kind,chunk),value in records.items():
        if kind==0x42:continue
        if kind==0x48:
            size,count,start=struct.unpack_from("<IBB",value)
            value=b"".join(records[(namespace,key,0x42,start+i)] for i in range(count))
            if len(value)!=size:raise ValueError("Blob index size mismatch")
            kind=0x41
        elif kind in (1,2,4,8,0x11,0x12,0x14,0x18):value=value[:kind&0xf]
        result[(namespace,key)]=(kind,value)
    return result
