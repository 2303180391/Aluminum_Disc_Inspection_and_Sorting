"""Production-C temporal tests; synthetic fixtures are not physical grasp evidence."""
import ctypes as C
import json
from pathlib import Path
import subprocess
import cv2
import numpy as np
from probe_origin_reference import load, detect, read_image
from test_fixed_origin import draw_discs, erase_marker

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tests'
GCC = r'D:\Qt\Tools\mingw1310_64\bin\gcc.exe'

def main():
    dll_path = HERE / 'locator_partial_occlusion.dll'
    subprocess.run([GCC, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared',
                    '-static-libgcc', str(ROOT/'firmware/src/disc_locator.c'), '-o', str(dll_path), '-lm'], check=True)
    core = load(dll_path)
    core.disc_locator_calibrated.restype = C.c_uint
    used = C.c_uint.in_dll(core, 'disc_locator_used_cache')
    empty = erase_marker(read_image(HERE/'fixtures/empty_frame_20261023.png'))
    rows = []

    def run(name, image, valid, xy=None, cached=None, reset=False):
        if reset:
            core.disc_locator_reset_calibration()
        r = detect(core, image)
        r['used_cache'] = used.value
        r['calibrated'] = core.disc_locator_calibrated()
        passed = bool(r['valid']) == valid
        if xy is not None and valid:
            r['error_mm'] = float(np.linalg.norm(np.array([r['x_um'],r['y_um']])/1000-xy))
            passed &= r['error_mm'] < 1.5 and bool(r['mm_valid'])
        if not valid:
            passed &= not r['mm_valid'] and r['x_um'] == r['y_um'] == 0
        if cached is not None:
            passed &= r['used_cache'] == int(cached)
        rows.append(dict(name=name, passed=bool(passed), **r))
        print(name, 'PASS' if passed else 'FAIL', 'status',r['status'], 'cache',r['used_cache'], 'xy',r['x_um'],r['y_um'])
        return r

    calibration = run('real_empty_calibration_no_grasp', empty, False, reset=True)
    assert calibration['calibrated'] and calibration['frame_valid']
    corners = calibration['corners_millipixel']
    def scene(discs):
        return draw_discs(empty, corners, discs)

    edge_disc = scene([(6,50,12)])
    run('cold_start_occluded_rejected', edge_disc, False, reset=True)
    for name, position in [('left',(6,50,12)),('right',(94,50,12)),
                           ('top',(50,94,12)),('bottom',(50,6,12))]:
        run('recalibrate_'+name, empty, False, reset=True)
        run('SYNTHETIC_'+name+'_partial_tape_cover',scene([position]),True,position[:2],cached=True)
    run('recalibrate_for_corner',empty,False,reset=True)
    run('SYNTHETIC_corner_contact_ambiguous_shape_rejected',scene([(10,10,14)]),False)
    run('full_clear_disc_unchanged', scene([(50,50,12)]), True, [50,50],cached=False)
    run('SYNTHETIC_empty_after_disc_is_not_normal',empty,False)
    run('SYNTHETIC_two_discs_one_on_edge',scene([(6,50,12),(65,65,10)]),False)
    run('recalibrate_for_outside',empty,False,reset=True)
    run('SYNTHETIC_centre_outside_workspace',scene([(-6,50,12)]),False)
    run('recalibrate_for_shift',empty,False,reset=True)
    moved = cv2.warpAffine(edge_disc,np.float32([[1,0,8],[0,1,0]]),(1280,720),borderValue=(65,140,40))
    run('SYNTHETIC_occluded_camera_shift_rejected',moved,False)
    run('recalibrate_for_no_frame',empty,False,reset=True)
    run('SYNTHETIC_missing_all_tape_rejected',np.full_like(empty,(65,140,40)),False)
    assert not core.disc_locator_calibrated()
    run('after_bad_pose_no_stale_cache',edge_disc,False)
    run('recalibrate_for_large_cover',empty,False,reset=True)
    run('SYNTHETIC_large_edge_cover_rejected',scene([(12,50,39)]),False)
    run('recalibrate_for_reset',empty,False,reset=True)
    run('reboot_or_reteach_invalidates_cache',edge_disc,False,reset=True)
    run('recalibrate_for_config',empty,False,reset=True)
    unconfirmed = detect(core,edge_disc,False)
    assert not unconfirmed['mm_valid'] and not unconfirmed['valid'] and not core.disc_locator_calibrated()
    report = dict(passed=all(row['passed'] for row in rows),cases=rows,
                  note='Synthetic replay of production C; not measured board accuracy, latency or successful grasp. Edge-overlap must still be physically reachable.')
    (ROOT/'evidence/partial_occlusion_offline_20261027.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    assert report['passed'], [r['name'] for r in rows if not r['passed']]
    print('PARTIAL_OCCLUSION_PASS cases='+str(len(rows)))

if __name__ == '__main__':
    main()
