#pragma once

namespace Assets {
inline constexpr auto DatasetUrl = "https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/almere_oosterwold_change_2016_2025_v1.zip";
inline constexpr auto DatasetArchive = "almere_oosterwold_change_2016_2025_v1.zip";
inline constexpr auto DatasetSha256 = "ecc35ef2c6bbdd586adc6ecc812ce1c74aee62fbf8e404de1d63b75bf7137a88";
inline constexpr auto DatasetCache = "oosterwold-2016-2025";
inline constexpr int BeforeYear = 2016, AfterYear = 2025;
inline constexpr auto BeforeFile = "oosterwold_2016-epsg25831-50cm.tif";
inline constexpr auto AfterFile = "oosterwold_2025-epsg25831-50cm.tif";
inline constexpr auto BeforeSha256 = "db723783db4c8738a716fe8b898de2e3099bab9394e335bd489db2412ae0c5c3";
inline constexpr auto AfterSha256 = "7b1b12d8b27cd0714f140415004daf9fd0000495eda31366c7cd4aab218caa82";
inline constexpr double DatasetBounds[] = {657533,5800382,658557,5801406};
inline constexpr auto ModelUrl = "https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/hotosm-dinov3s-buildings-geokernel-onnx-v1.zip";
inline constexpr auto ModelArchive = "hotosm-dinov3s-buildings-geokernel-onnx-v1.zip";
inline constexpr auto ModelSha256 = "3a6cd11e013f81b46b3db700feddef2d98319780285b8320e1200573684b24bb";
inline constexpr auto ModelId = "hotosm-dinov3s-buildings";
inline constexpr auto Attribution = "Imagery: Beeldmateriaal Nederland / PDOK, 2016 / 2025, CC BY 4.0. WMS JPEG crop/reprojection rendered at 0.5 m and converted to GeoTIFF. Model: HOTOSM / Meta DINOv3; see model package licenses.";
}
