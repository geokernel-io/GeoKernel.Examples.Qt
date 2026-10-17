#pragma once
#include <GeoKernel.Globe/RuntimeWindow.h>
#include <QCoreApplication>

namespace GlobeSample {
namespace Runtime = GeoKernel3D::GlobeRuntime;

// Sample policy only; renderer and navigation remain entirely in the SDK.
inline Runtime::RuntimeOptions offlineOptions()
{
    Runtime::RuntimeOptions options;
    options.networkEnabled = false;
    options.initialTileSource = -1;
    options.terrainEnabled = false;
    options.terrainExaggeration = 1.0f;
    options.atmosphereEnabled = false;
    options.spaceBackgroundEnabled = false;
    options.sunlight = false;
    return options;
}

inline void initializeDataPaths()
{
    const auto root = QCoreApplication::applicationDirPath();
    qputenv("PROJ_DATA", (root + "/proj-data").toUtf8());
    qputenv("GDAL_DATA", (root + "/gdal-data").toUtf8());
    qputenv("GDAL_DRIVER_PATH", (root + "/gdalplugins").toUtf8());
}

}
