"""Read native captures through the actual C locator, never a Python detector."""
import ctypes as C
import json
from pathlib import Path
import sys
import cv2
import numpy as np

FIELDS = ['valid', 'candidates', 'x_millipixel', 'y_millipixel', 'area_samples',
          'status', 'frame_valid', 'origin_valid', 'mm_valid', 'x_um', 'y_um',
          'x_ppm', 'y_ppm']

class Result(C.Structure):
    _fields_ = [(n, C.c_int32 if n in ('x_um', 'y_um') else C.c_uint32) for n in FIELDS] + [
        ('corners_millipixel', C.c_int32 * 8), ('frame_candidates', C.c_uint32), ('marker_pixels', C.c_uint32)]

class Config(C.Structure):
    _fields_ = [('width_um', C.c_uint32), ('height_um', C.c_uint32), ('dimensions_confirmed', C.c_uint32)]

def load(path):
    dll = C.CDLL(str(Path(path).resolve()))
    dll.disc_locate_config.argtypes = [C.c_void_p, C.POINTER(Config), C.POINTER(Result)]
    return dll

def detect(dll, image, confirmed=True):
    image = np.ascontiguousarray(image)
    assert image.shape == (720, 1280, 3) and image.dtype == np.uint8
    result = Result()
    config = Config(100000, 100000, int(confirmed))
    dll.disc_locate_config(image.ctypes.data, C.byref(config), C.byref(result))
    data = {n: getattr(result, n) for n in FIELDS}
    data.update(corners_millipixel=list(result.corners_millipixel),
                frame_candidates=result.frame_candidates, marker_pixels=result.marker_pixels)
    return data

def read_image(path):
    image = cv2.imdecode(np.fromfile(path, dtype=np.uint8), cv2.IMREAD_COLOR)
    assert image is not None, path
    return image

if __name__ == '__main__':
    dll = load(sys.argv[1])
    for item in sys.argv[2:]:
        data = detect(dll, read_image(item))
        print(json.dumps(dict(path=item, **data)))
