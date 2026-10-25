#pragma once
#include <array>
inline constexpr const char* ModelUrl="https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/fieldsoftheworld-prue-efficientnet-b5-ccby-geokernel-onnx-v1.zip";
inline constexpr const char* ModelArchive="fieldsoftheworld-prue-efficientnet-b5-ccby-geokernel-onnx-v1.zip";
inline constexpr const char* ModelSha="fb38874260ab6ce6104e8b1796bcbd5651e0b3dfd5aeef8ae79e77f421363434";
inline constexpr const char* DatasetUrl="https://raw.githubusercontent.com/fieldsoftheworld/ftw-baselines/779b809813b301de7fb9f00f0c5db07086b23227/tests/data-files/inference-img.tif";
inline constexpr const char* DatasetSha="da482e7987b36898bd9f8381e7c7fe95c05dfc5ccbba19a58eb65e544d1af750";
inline const std::array<const char*,3> ClassNames{{"Background","Field interior","Field boundary"}};
inline const std::array<const char*,3> ClassColors{{"#59636e","#4caf50","#ffce32"}};
