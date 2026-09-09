# ObjectDetectionInference

Runs the NWPU-VHR-10 Faster R-CNN GeoKernel `object_detection` ONNX package
on aerial RGB images. At startup the example downloads and extracts:

- `object_detection_images.zip` (34 selected NWPU-VHR-10 JPG images)
- `object_detection_model.zip` (ONNX model and `geokernel-model.json`)

Use **Previous image** and **Next image** to move through the sample set, then
run inference for the selected image.

Inference uses the same sliding-window strategy as the Python reference:
`512 x 512` tiles, `256` pixels of overlap, followed by global class-aware NMS
at IoU `0.3`. This preserves small objects instead of shrinking the complete
image into one model input.

An ONNX tile can legitimately return zero detections (`[0,4]`, `[0]`, `[0]`).
GeoKernel SDK 1.5.14 reports zero-length runtime tensors as an error, so this
specific condition is treated as an empty tile while all other inference
errors remain fatal.

Detected objects are materialized as an on-the-fly polygon layer. Each feature
contains `class_id`, `label`, `score`, and pixel bounding-box attributes. The
layer style displays the `label` attribute as the class-name label above each
bounding box. JPG images without georeferencing use a pixel-space transform
whose Y axis is flipped from the image's top-left origin to the viewer's
negative viewer Y coordinates, so detections align with the source image.

A categorized renderer uses `class_id` to assign a stable color to each of the
ten NWPU-VHR-10 foreground classes. Class colors therefore remain consistent
when navigating between images.

## Build

Run `build-windows.bat`, or configure the project with CMake and GeoKernel 1.5.9 or newer. Until that release is published, point `GEOKERNEL_ROOT` at a local SDK build containing the generic object-detection APIs.
