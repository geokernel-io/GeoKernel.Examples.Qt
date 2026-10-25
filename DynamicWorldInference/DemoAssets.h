#pragma once
#include <array>
inline constexpr const char* ModelUrl="https://pub-00d214f25f754884b22349c19ac37bbf.r2.dev/geokernel-models/google-dynamicworld-geokernel-onnx-v1.zip";
inline constexpr const char* ModelArchive="google-dynamicworld-geokernel-onnx-v1.zip";
inline constexpr const char* ModelSha="326bb2899cdea6e620a02f5e16fd330ec848e0a5b76f72f576038d7bc1e0aa63";
inline constexpr const char* DatasetUrl="https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/spain_sentinel2_l1c.zip";
inline constexpr const char* DatasetArchive="spain_sentinel2_l1c.zip";
inline constexpr const char* DatasetSha="07811e9600882c526185268c5b280134e5715c95042754e414d6736fa5647839";
inline const std::array<const char*,9> ClassNames{{"Water","Trees","Grass","Flooded vegetation","Crops","Shrub and scrub","Built","Bare","Snow and ice"}};
inline const std::array<const char*,9> ClassColors{{"#419bdf","#397d49","#88b053","#7a87c6","#e49635","#dfc35a","#c4281b","#a59b8f","#b39fe1"}};
