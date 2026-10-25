#pragma once
#include <array>
inline constexpr const char* ModelUrl="https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/ibm-nasa-prithvi-eo2-300m-tl-sen1floods11-geokernel-onnx-v1.zip";
inline constexpr const char* ModelArchive="ibm-nasa-prithvi-eo2-300m-tl-sen1floods11-geokernel-onnx-v1.zip";
inline constexpr const char* ModelSha="34711ae55d48fe265b19c18551aef4b4aa84045a07bffb407161570b19429c13";
inline constexpr const char* DatasetUrl="https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/flood_analysis.zip";
inline constexpr const char* DatasetArchive="flood_analysis.zip";
inline constexpr const char* DatasetSha="059e9a84bed08ea5da76941432e6e8409bd1352661ff72f750e47df5f740c38f";
inline const std::array<const char*,2> ClassNames{{"Non-water","Water / flood"}};
inline const std::array<const char*,2> ClassColors{{"#c7b79e","#168bff"}};
