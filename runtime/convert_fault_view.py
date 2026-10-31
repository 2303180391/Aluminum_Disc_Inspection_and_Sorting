"""Convert two identical frozen current-view reads; never label as failed input."""
from pathlib import Path
import hashlib
import json
import os
import sys
import zlib
from PIL import Image

def convert(first,second,mail,prefix):
    data=Path(first).read_bytes()
    if len(data)!=1280*720*3 or data!=Path(second).read_bytes():
        raise ValueError('Incomplete or inconsistent frozen-view reads; no PNG accepted')
    words=[int(v,0) for v in Path(mail).read_text(encoding='utf-8-sig').split()]
    if len(words)!=64 or words[:4]!=[0x444C4332,3,0x20261023,64] or words[5]!=2 or words[57] not in (0x45444731,0x45444732):
        raise ValueError('Wrong EDG1 locator metadata')
    png=Path(str(prefix)+'.png');meta=Path(str(prefix)+'.json')
    for p in [png,meta,png.with_suffix('.partial.png'),meta.with_suffix('.partial.json')]:
        if p.exists():raise FileExistsError(p)
    record=dict(format='arm5_current_held_fault_view_1280x720_bgr',
                exact_input_of_failed_locate=False,
                note='Current view captured AFTER the recorded failure while arm commands remain locked. Previous result is context only, not computed on this captured image.',
                width=1280,height=720,byte_order='BGR',crc32=f'{zlib.crc32(data)&0xffffffff:08X}',
                raw_sha256=hashlib.sha256(data).hexdigest(),raw=str(first),verification_raw=str(second),
                previous_locator_sequence=words[6],previous_geometry=words[21],
                previous_frame_valid=words[22],previous_origin_valid=words[23],
                previous_disc_valid=words[16],reference_tag=f'{words[52]:08X}',mailbox=words)
    Image.frombytes('RGB',(1280,720),data,'raw','BGR',3840,1).save(png.with_suffix('.partial.png'))
    meta.with_suffix('.partial.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    os.rename(png.with_suffix('.partial.png'),png)
    os.rename(meta.with_suffix('.partial.json'),meta)
    return record

if __name__=='__main__':
    if len(sys.argv)!=5:raise SystemExit('usage: convert_fault_view.py RAW VERIFY MAIL PREFIX')
    r=convert(*sys.argv[1:])
    print('FAULT_VIEW_CONVERT_OK '+sys.argv[4]+'.png crc=0x'+r['crc32'])
