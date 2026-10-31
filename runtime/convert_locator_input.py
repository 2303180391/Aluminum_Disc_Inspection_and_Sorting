"""Decode only CRC-verified, matching locator v3 input/result snapshots."""
import json
import os
from pathlib import Path
import sys
import zlib
from PIL import Image

def convert(raw_a,raw_b,mail,prefix):
    data=Path(raw_a).read_bytes()
    second=Path(raw_b).read_bytes()
    words=[int(v,0) for v in Path(mail).read_text(encoding='utf-8-sig').split()]
    if len(words)!=64 or words[:4]!=[0x444C4332,3,0x20261023,64]:
        raise ValueError('Wrong or incomplete locator v3 mailbox')
    if words[5]!=2 or not words[7] or not words[7]==words[47]==words[63]:
        raise ValueError('Snapshot/result request identifiers do not match')
    if words[44]!=0x08400000 or words[45]!=2764800:
        raise ValueError('Wrong snapshot address or length')
    if len(data)!=2764800 or data!=second:
        raise ValueError('Incomplete or inconsistent JTAG reads; no PNG accepted')
    crc=zlib.crc32(data)&0xffffffff
    if not crc==words[46]==words[49]==words[50]:
        raise ValueError('Board source/snapshot or host CRC mismatch; no PNG accepted')
    fields=['valid','candidates','x_millipixel','y_millipixel','area_samples',
            'status','frame_valid','origin_valid','mm_valid','x_um','y_um','x_ppm','y_ppm']
    result=dict(zip(fields,words[16:29]))
    result['corners_millipixel']=words[29:37]
    result['frame_candidates']=words[37];result['marker_pixels']=words[38]
    record=dict(format='locator_v3_exact_input_1280x720_bgr',width=1280,height=720,
                raw_byte_order='BGR',crc32=f'{crc:08X}',request=words[7],sequence=words[6],
                exact_same_input_as_board_locator=True,board_result=result,
                capture_fence_us=words[8],compute_us=words[9],total_us=words[10],snapshot_copy_crc_us=words[48],
                inner_width_um=words[12],inner_height_um=words[13],dimensions_confirmed=words[14],
                raw=str(raw_a),verification_raw=str(raw_b),mailbox=words)
    png=Path(str(prefix)+'.png');metadata=Path(str(prefix)+'.json')
    for p in [png,metadata,png.with_suffix('.partial.png'),metadata.with_suffix('.partial.json')]:
        if p.exists(): raise FileExistsError(p)
    image=Image.frombytes('RGB',(1280,720),data,'raw','BGR',3840,1)
    image.save(png.with_suffix('.partial.png'))
    metadata.with_suffix('.partial.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    os.rename(png.with_suffix('.partial.png'),png)
    os.rename(metadata.with_suffix('.partial.json'),metadata)
    return record

if __name__=='__main__':
    if len(sys.argv)!=5: raise SystemExit('usage: convert_locator_input.py RAW_A RAW_B MAIL PREFIX')
    r=convert(*sys.argv[1:])
    print(f"LOCATOR_INPUT_CONVERT_OK {sys.argv[4]}.png crc=0x{r['crc32']}")
