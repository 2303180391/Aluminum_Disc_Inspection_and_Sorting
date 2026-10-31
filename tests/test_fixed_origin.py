"""Offline production-C replay. Synthetic mutations are test fixtures, not board evidence."""
import hashlib
import json
from pathlib import Path
import subprocess
import cv2
import numpy as np
from probe_origin_reference import load, detect, read_image

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tests'
GCC = Path(r'D:\Qt\Tools\mingw1310_64\bin\gcc.exe')

def compile_core(name, fixed):
    target = HERE / name
    subprocess.run([str(GCC), '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-static-libgcc',
                    '-DDISC_FIXED_ORIGIN=' + str(fixed), str(ROOT / 'firmware/src/disc_locator.c'),
                    '-o', str(target), '-lm'], check=True)
    return load(target)

def erase_marker(image):
    out = image.copy()
    patch = out[512:587, 378:454]
    neutral = (patch.min(axis=2) > 100) & ((patch.max(axis=2).astype(int) - patch.min(axis=2)) < 65)
    patch[neutral] = (35, 40, 35)
    return out

def draw_discs(background, corners, positions):
    out = erase_marker(background)
    matrix = cv2.getPerspectiveTransform(np.float32([[0,0],[100,0],[100,100],[0,100]]),
                                        np.float32(corners).reshape(4,2) / 1000)
    for xx, yy, radius in positions:
        theta = np.linspace(0, 2*np.pi, 240)
        contour = np.float32(np.column_stack([xx+radius*np.cos(theta), yy+radius*np.sin(theta)]))[None]
        contour = cv2.perspectiveTransform(contour, matrix)[0]
        cv2.fillPoly(out, [np.int32(np.round(contour))], (180,185,190))
    return out

def main():
    reference = ROOT / 'reference/camera720p_20261023_160326_349.png'
    config = json.loads((ROOT / 'reference/fixed_origin_20261027.json').read_text(encoding='utf-8'))
    assert hashlib.sha256(reference.read_bytes()).hexdigest().upper() == config['sha256']
    fixed, marker = compile_core('locator_fixed_origin.dll', 1), compile_core('locator_marker_baseline.dll', 0)
    image = read_image(reference)
    original = detect(marker, image)
    assert original['valid'] and original['origin_valid']
    assert original['corners_millipixel'] == np.rint(np.array(config['reference_corners_native_pixel']).reshape(-1)*1000).astype(int).tolist()
    rows = []

    def record(name, changed, expected=True, xy=None, status=None, confirmed=True):
        fixed.disc_locator_reset_calibration()  # independent cold-start baseline fixture
        data = detect(fixed, changed, confirmed)
        passed = bool(data['valid']) == expected
        if expected:
            passed = passed and bool(data['frame_valid'] and data['origin_valid']) and data['mm_valid'] == int(confirmed)
            if xy is not None:
                error = float(np.linalg.norm(np.array([data['x_um'], data['y_um']])/1000-xy))
                data['test_error_mm'] = error
                passed = passed and error < 1.5
        else:
            passed = passed and not data['mm_valid'] and not data['x_um'] and not data['y_um']
        if status is not None:
            passed = passed and data['status'] == status
        rows.append(dict(name=name, passed=bool(passed), **data))
        print(name, 'PASS' if passed else 'FAIL', 'geometry', data['status'], 'xy', data['x_um'], data['y_um'])
        return data

    target = [original['x_um']/1000, original['y_um']/1000]
    same = record('real_user_reference_exact_coordinates', image, xy=target)
    assert same['corners_millipixel'] == original['corners_millipixel']
    assert (same['x_um'],same['y_um']) == (original['x_um'],original['y_um'])
    removed = erase_marker(image)
    baseline_removed = detect(marker, removed)
    assert baseline_removed['status'] == 3 and not baseline_removed['origin_valid'], baseline_removed
    record('SYNTHETIC_marker_removed_from_real_reference', removed, xy=target)
    # Add competing bright patches OUTSIDE the frame: origin must not follow them.
    competing = removed.copy()
    q = np.array(original['corners_millipixel']).reshape(4,2)/1000
    centre = q.mean(axis=0)
    for corner in q:
        position = corner+(corner-centre)/np.linalg.norm(corner-centre)*24
        xx,yy = np.rint(position).astype(int)
        cv2.rectangle(competing, (xx-10,yy-10), (xx+10,yy+10), (245,245,245), -1)
    record('SYNTHETIC_competing_white_marks_do_not_change_origin', competing, xy=target)
    for factor in (0.8, 1.1):
        changed = np.uint8(np.clip(removed.astype(float)*factor,0,255))
        changed &= np.array([248,252,248],np.uint8)
        # Do not claim this origin-only change fixes the existing colour-mask
        # sensitivity. Compare to the old detector under the SAME exposure,
        # and separately disclose its drift relative to the reference capture.
        old_exposure = np.uint8(np.clip(image.astype(float)*factor,0,255))
        old_exposure &= np.array([248,252,248],np.uint8)
        baseline = detect(marker, old_exposure)
        assert baseline['valid'], baseline
        baseline_xy = [baseline['x_um']/1000,baseline['y_um']/1000]
        data = record('SYNTHETIC_RGB565_exposure_baseline_parity_'+str(factor), changed, xy=baseline_xy)
        data['unchanged_baseline_drift_mm'] = float(np.linalg.norm(np.array(baseline_xy)-target))
        data['baseline_exposure_xy_mm'] = baseline_xy
        print('  existing segmentation exposure drift mm:',data['unchanged_baseline_drift_mm'])
    translated = cv2.warpAffine(removed, np.float32([[1,0,20],[0,1,-12]]), (1280,720), borderValue=(65,140,40))
    record('SYNTHETIC_small_camera_shift_coordinates_not_frozen', translated, xy=target)
    moved = cv2.warpAffine(removed, np.float32([[1,0,80],[0,1,0]]), (1280,720), borderValue=(65,140,40))
    record('SYNTHETIC_large_shift_rejected', moved, expected=False, status=7)
    for scale,angle in ((1.2,0),(1,20)):
        matrix = cv2.getRotationMatrix2D(tuple(centre), angle, scale)
        changed = cv2.warpAffine(removed, matrix, (1280,720), borderValue=(65,140,40))
        record('SYNTHETIC_pose_rejected_'+str(scale)+'_'+str(angle), changed, expected=False)
    record('unconfirmed_dimensions_no_valid_mm', removed, xy=target, confirmed=False)
    empty = read_image(HERE / 'fixtures/empty_frame_20261023.png')
    empty_original = detect(marker, empty)
    assert empty_original['frame_valid'] and empty_original['origin_valid'] and not empty_original['valid']
    record('real_empty_workspace_not_a_normal_workpiece', erase_marker(empty), expected=False, status=4)
    for xx in (20,50,80):
        for yy in (20,50,80):
            changed = draw_discs(empty, empty_original['corners_millipixel'], [(xx,yy,7)])
            record('SYNTHETIC_grid_'+str(xx)+'_'+str(yy), changed, xy=[xx,yy])
    changed = draw_discs(empty, empty_original['corners_millipixel'], [(25,30,7),(75,70,7)])
    record('SYNTHETIC_two_discs_rejected', changed, expected=False, status=5)
    changed = draw_discs(empty, empty_original['corners_millipixel'], [(2,50,10)])
    record('SYNTHETIC_border_clipped_disc_rejected', changed, expected=False)
    record('SYNTHETIC_no_frame_rejected', np.full((720,1280,3),(65,140,40),np.uint8), expected=False, status=1)
    old_capture = read_image(HERE / 'fixtures/real_disc_20261023_163514.png')
    record('real_previous_board_capture_axes_preserved', old_capture, xy=[49.738,54.111])
    report = dict(reference_sha256=config['sha256'], marker_removed_baseline=baseline_removed,
                  passed=all(row['passed'] for row in rows), cases=rows,
                  note='Same production C core. Synthetic image mutations and host replay are not physical accuracy, grasp or FPS evidence.')
    (ROOT / 'evidence/fixed_origin_offline_20261027.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    assert report['passed'], [row['name'] for row in rows if not row['passed']]
    print('FIXED_ORIGIN_REPLAY_PASS cases='+str(len(rows)))

if __name__ == '__main__':
    main()
