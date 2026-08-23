# ObjectDetectionInference

Runs a generic GeoKernel `object_detection` ONNX model package on a georeferenced RGB GeoTIFF. The example reads tensor names, preprocessing, box format, score threshold, NMS settings and labels from `geokernel-model.json`; it contains no model-specific inference code.

Detected objects are converted to georeferenced bounding-box polygons and displayed directly from an in-memory layer over the source raster.

## Sample model

The example is compatible with the packaged ONNX Model Zoo SSD COCO model at:

`D:\projects\GeoKernel Datasets\BuildingSegmentation\Models\SsdCoco`

SSD COCO is intended for ordinary perspective imagery. It validates the generic object-detection adapter, but is not an aerial-building detector and should not be evaluated with NAIP imagery.

## Build

Run `build-windows.bat`, or configure the project with CMake and GeoKernel 1.5.9 or newer. Until that release is published, point `GEOKERNEL_ROOT` at a local SDK build containing the generic object-detection APIs.
