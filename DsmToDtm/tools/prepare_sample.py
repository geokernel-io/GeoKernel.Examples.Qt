"""Maintainer-only packaging of frozen results; the Qt application needs no Python.

python tools/prepare_sample.py --models-root <models-folder> --proj-data <PROJ-9.8.1-data-folder>
Outputs an immutable upload ZIP and updates the example's resource hash pins.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import zipfile

# Use rasterio's own data, never an inherited Conda/ArcGIS PROJ database.
for variable in ('PROJ_LIB', 'PROJ_DATA', 'GDAL_DATA'):
    os.environ.pop(variable, None)
import numpy as np
import rasterio
from rasterio.windows import Window


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--models-root', type=Path, required=True)
    parser.add_argument('--proj-data', type=Path, required=True,
                        help='PROJ 9.8.1 data directory matching the published SDK proj_9.dll')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    models = args.models_root
    gallery_path = models / 'validation/dsm2dtm-sdk-1.5.40-v3/gallery.json'
    gallery = json.loads(gallery_path.read_text(encoding='utf-8'))
    scene = next(s for s in gallery['scenes'] if s['id'] == 'zurich-2677-1251')
    ai = scene['ai_results']['pix_grad']
    focus = scene['presentation']['focus_window']
    window = Window(focus['col'], focus['row'], focus['size'], focus['size'])
    archive_name = 'zurich_dsm_to_dtm_v2.zip'
    output = root / 'outputs/sample-upload'
    content = output / 'content'
    content.mkdir(parents=True, exist_ok=True)
    sources = {
        'dsm.tif': (Path(scene['inference']['source_dsm']), scene['inference']['source_dsm_sha256']),
        'dtm_classical.tif': (models / 'validation/dsm2dtm-baseline-v3/zurich-2677-1251/dtm_estimated.tif', scene['presentation']['estimated_sha256']),
        'dtm_ai.tif': (models / 'validation/dsm2dtm-sdk-1.5.40-v3/zurich-2677-1251/pix_grad/dtm_estimated.tif', ai['presentation']['estimated_sha256']),
        'dtm_reference.tif': (Path(scene['reference_path']), scene['reference_sha256']),
    }
    arrays, source_hashes = {}, {}
    grid = None
    for name, (source, expected) in sources.items():
        assert digest(source) == expected, f'Frozen input changed: {source}'
        source_hashes[name] = {'filename': source.name, 'full_tile_sha256': expected}
        with rasterio.open(source) as src:
            assert src.crs == rasterio.crs.CRS.from_epsg(2056), 'Refusing to package an incomplete or unknown source CRS.'
            data = src.read(1, window=window, masked=True)
            assert not np.any(np.ma.getmaskarray(data)), 'This first demonstration requires a fully valid crop.'
            assert np.isfinite(data).all()
            transform = src.window_transform(window)
            identity = (src.crs, transform, data.shape)
            if grid is None:
                grid = identity
                profile = dict(driver='GTiff', height=data.shape[0], width=data.shape[1], count=1,
                               dtype='float32', crs=src.crs, transform=transform, nodata=-9999.,
                               compress='DEFLATE', predictor=3, tiled=True)
            assert identity == grid
            arrays[name] = np.asarray(data, dtype=np.float32)
            with rasterio.open(content / name, 'w', **profile) as dst:
                dst.write(arrays[name], 1)
                dst.update_tags(SOURCE='Federal Office of Topography swisstopo', HEIGHT_UNIT='metre',
                                VERTICAL_CRS_SOURCE_DECLARED='EPSG:5728 / LN02',
                                ROLE='reference-evaluation-only' if name == 'dtm_reference.tif' else name,
                                REFERENCE_USED_FOR_INFERENCE='false', PREPARED_RESULT='true')
    neutral = {**profile, 'count': 3, 'dtype': 'uint8', 'nodata': None, 'predictor': 2}
    with rasterio.open(content / 'neutral_surface.tif', 'w', **neutral) as dst:
        dst.write(np.full((3, 500, 500), 220, dtype=np.uint8))
        dst.update_tags(ROLE='Synthetic constant albedo for identical neutral lighting; not an orthophoto')
    database = args.proj_data.resolve() / 'proj.db'
    with sqlite3.connect(database.as_uri() + '?mode=ro', uri=True) as connection:
        database_metadata = dict(connection.execute('select key,value from metadata'))
    assert database_metadata['PROJ.VERSION'] == '9.8.1'
    (content / 'proj-data').mkdir(exist_ok=True)
    shutil.copyfile(database, content / 'proj-data/proj.db')
    shutil.copyfile(root / 'tools/PROJ-COPYING.txt', content / 'PROJ-LICENSE.txt')
    reference = arrays['dtm_reference.tif'].astype(np.float64)
    metrics = {}
    for name, key in [('dsm.tif', 'dsm'), ('dtm_classical.tif', 'classical'), ('dtm_ai.tif', 'ai')]:
        error = arrays[name].astype(np.float64) - reference
        metrics[key] = {'count': int(error.size), 'rmse_m': float(np.sqrt(np.mean(error ** 2))),
                        'mae_m': float(np.mean(np.abs(error))), 'bias_m': float(error.mean())}
    pair = json.loads((models / 'demo-data/dsm2dtm-switzerland/prepared/zurich-2677-1251/pair.json').read_text(encoding='utf-8'))
    attribution = '''Federal Office of Topography swisstopo
DSM: swissSURFACE3D Raster, source filename year 2018.
Reference DTM: swissALTI3D, source filename year 2019.
OGD terms: https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices
Source attribution is mandatory. The supplied height rasters are cropped/derived data.
Classical result: GeoKernel roughness-aware morphological/TIN prototype v3.
AI result: David Perez, DSM-only pix_grad experimental pretrained model;
https://doi.org/10.5281/zenodo.7973800 ; https://github.com/davpergan/DTM_DSM_GAN
Model weights: CC BY 4.0; no weights are distributed in this archive.
Prepared AI inference: GeoKernel SDK 1.5.40, CPU; no reference input.
This fixed urban crop is an illustrative development/training scene, not a held-out benchmark.
Coordinate database: PROJ contributors / OSGeo, PROJ 9.8.1; see PROJ-LICENSE.txt.
'''
    (content / 'ATTRIBUTION.txt').write_text(attribution, encoding='utf-8')
    shutil.copyfile(models / 'davpergan/dtm-dsm-gan-pix-grad/zenodo-7973800/geokernel-onnx-v1/LICENSE-weights-CC-BY-4.0.txt', content / 'MODEL-LICENSE-CC-BY-4.0.txt')
    (content / 'README.txt').write_text('''DSM to DTM: ground beneath buildings / Zurich
500 x 500 Float32 height pixels at 0.5 m; 250 x 250 m, EPSG:2056 / source LN02 metres.
dsm.tif: original observed surface (roofs, trees, ground).
dtm_classical.tif: prepared classical estimate, not a new inference input.
dtm_ai.tif: prepared DSM-only pix_grad AI estimate, independent of the classical output.
dtm_reference.tif: provider terrain, used for scoring/profile only; not fed to either method.
neutral_surface.tif: constant synthetic RGB for identical surface albedo; not imagery.
proj-data/proj.db: matching PROJ 9.8.1 coordinate database for the native SDK runtime.
source-manifest.json: grid, sources, hashes, methods and crop-only scores.
ATTRIBUTION.txt / MODEL-LICENSE-CC-BY-4.0.txt: retain these credits when redistributing.
Prepared outputs were inferred on the complete 1 km source tile before cropping, preserving context.
The app interpolates heights for display: (1-t)*DSM+t*selected_DTM. Intermediate frames are not DTM outputs.
Source filename years differ (2018/2019); matching acquisition dates are not established.
The viewer treats LN02 heights as an explicitly labelled local preview; no geoid conversion is performed.
''', encoding='utf-8')
    files = {p.relative_to(content).as_posix(): digest(p) for p in sorted(content.rglob('*'))
             if p.is_file() and p.name != 'source-manifest.json'}
    manifest = {
        'schema_version': 1, 'id': 'zurich-dsm-to-dtm-v2', 'title': 'Ground beneath buildings / Zurich',
        'width': 500, 'height': 500, 'pixel_size_m': .5, 'horizontal_crs': 'EPSG:2056',
        'vertical_reference': 'EPSG:5728 / LN02, source declared', 'height_unit': 'metre',
        'bounds': [2677100., 1251350., 2677350., 1251600.], 'geotransform': list(grid[1].to_gdal()),
        'source_dsm_url': pair['dsm_url'], 'source_reference_url': pair['dtm_url'],
        'source_filename_years': [2018, 2019], 'same_acquisition_epoch_verified': False,
        'focus_selection': scene['presentation']['focus_selection'], 'split': scene['split'],
        'prepared_results': True, 'inference_on_full_tile_before_crop': True,
        'reference_used_for_inference': False, 'reference_coverage_fraction': 1.,
        'classical': {'method': scene['inference']['method'], 'parameters': scene['inference']['parameters'],
                      'script_sha256': scene['inference']['script_sha256']},
        'ai': {'profile': 'pix_grad', 'inputs': ['DSM'], 'model_id': ai['inference']['model_id'],
               'model_sha256': ai['inference']['model_sha256'], 'source': 'https://doi.org/10.5281/zenodo.7973800',
               'sdk_version': '1.5.40', 'experimental': True, 'weights_included': False},
        'metrics': metrics, 'metric_scope': 'This fixed 250 m development crop only; shared valid reference pixels.',
        'profile': {'row': focus['line_row'] - focus['row'], 'sampling': 'original pixel centres'},
        'full_tile_sources': source_hashes, 'files': files,
        'projection_database': {'source': 'PROJ 9.8.1 / OSGeo', 'metadata': database_metadata,
                                'sha256': digest(database), 'purpose': 'Native coordinate transformations; not terrain observations.'},
    }
    (content / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    files['source-manifest.json'] = digest(content / 'source-manifest.json')
    archive = output / archive_name
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 6, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, (content / name).read_bytes())
    assets = {'url': 'https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/' + archive_name,
              'archive': archive_name, 'cache': 'zurich-dsm-to-dtm-v2', 'bytes': archive.stat().st_size,
              'sha256': digest(archive), 'files': files}
    (root / 'sample-assets.json').write_text(json.dumps(assets, indent=2) + '\n', encoding='utf-8')
    (output / (archive_name + '.sha256')).write_text(f'{digest(archive)}  {archive_name}\n')
    print(json.dumps({'archive': str(archive), 'bytes': assets['bytes'], 'sha256': assets['sha256'], 'metrics': metrics}, indent=2))


if __name__ == '__main__':
    main()
