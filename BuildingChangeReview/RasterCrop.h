#pragma once
#include <algorithm>
#include <cmath>
#include <QImage>
#include <QFileInfo>
#include "GeoKernelAI.h"
#include "Common/Gdal/GdalRasterProvider.h"

// Use SDK APIs only: no GDAL development headers, command-line GDAL or Python.
// The provider reads RGB bytes into QImage; the SDK writer stores the same 0..255
// values as Float32 with RGB interpretation and a scaled affine geotransform.
inline QString prepareInferenceRaster(const QString& input, const QString& output, int maximumSide,
    const GeoKernel::Core::Shapes::GisExtent& region = {}) {
    using namespace GeoKernel::Formats::Common::Gdal;
    using namespace GeoKernel::AI;
    GdalRasterProvider provider;
    QString error;
    if (!provider.open(input, &error)) throw std::runtime_error(error.toStdString());
    const auto metadata = provider.metadata();
    if (!metadata.hasWorldTransform || metadata.bandCount < 3)
        throw std::runtime_error("The demo requires a georeferenced RGB raster.");
    int left=0, top=0, sourceWidth=metadata.width, sourceHeight=metadata.height;
    const auto& t = metadata.worldTransform;
    const double originX=t.upperLeftCenterX-(t.pixelSizeX+t.rotationY)*.5;
    const double originY=t.upperLeftCenterY-(t.rotationX+t.pixelSizeY)*.5;
    if (!region.isEmpty()) {
        const double determinant=t.pixelSizeX*t.pixelSizeY-t.rotationX*t.rotationY;
        if (determinant==0) throw std::runtime_error("Raster transform is singular.");
        double minX=metadata.width,minY=metadata.height,maxX=0,maxY=0;
        for(double x:{region.xMin(),region.xMax()}) for(double y:{region.yMin(),region.yMax()}) {
            const double dx=x-originX,dy=y-originY;
            const double px=(dx*t.pixelSizeY-dy*t.rotationY)/determinant;
            const double py=(dy*t.pixelSizeX-dx*t.rotationX)/determinant;
            minX=std::min(minX,px);maxX=std::max(maxX,px);minY=std::min(minY,py);maxY=std::max(maxY,py);
        }
        left=std::clamp(static_cast<int>(std::floor(minX)),0,metadata.width);
        top=std::clamp(static_cast<int>(std::floor(minY)),0,metadata.height);
        sourceWidth=std::clamp(static_cast<int>(std::ceil(maxX)),0,metadata.width)-left;
        sourceHeight=std::clamp(static_cast<int>(std::ceil(maxY)),0,metadata.height)-top;
        if(sourceWidth<=0 || sourceHeight<=0) throw std::runtime_error("The current view does not intersect the raster.");
    }
    if (region.isEmpty() && metadata.width <= maximumSide && metadata.height <= maximumSide) return input;
    const double scale = std::min(1.,static_cast<double>(maximumSide) / std::max(sourceWidth, sourceHeight));
    const int width = std::max(1, qRound(sourceWidth * scale));
    const int height = std::max(1, qRound(sourceHeight * scale));
    GdalRasterWindowRequest request;
    request.sourceX=left;request.sourceY=top;
    request.sourceWidth = sourceWidth; request.sourceHeight = sourceHeight;
    request.targetWidth = width; request.targetHeight = height;
    const QImage image = provider.readWindow(request, &error).convertToFormat(QImage::Format_RGB888);
    if (image.isNull()) throw std::runtime_error(error.toStdString());
    AiRasterInferenceResult raster;
    raster.width = width; raster.height = height; raster.bandCount = 3;
    raster.dataType = AiTensorDataType::Float32;
    const qint64 plane = static_cast<qint64>(width) * height;
    raster.data.resize(plane * 3 * sizeof(float));
    float* values = reinterpret_cast<float*>(raster.data.data());
    for (int y = 0; y < height; ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
                values[c * plane + y * width + x] = row[x * 3 + c];
    }
    const double sx = static_cast<double>(sourceWidth) / width;
    const double sy = static_cast<double>(sourceHeight) / height;
    raster.geoTransform = {
        originX+left*t.pixelSizeX+top*t.rotationY,
        t.pixelSizeX * sx, t.rotationY * sy,
        originY+left*t.rotationX+top*t.pixelSizeY,
        t.rotationX * sx, t.pixelSizeY * sy};
    raster.projectionWkt = metadata.projectionWkt;
    AiRasterInferenceExecutor::writeGeoTiff(raster, output);
    return output;
}
