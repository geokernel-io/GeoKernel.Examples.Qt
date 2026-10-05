#pragma once
#include <GeoKernel.Viewer3D/ModelPlacement.h>
#include <GeoKernel.Viewer3D/Viewshed.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Clips DEM faces to classified sample cells; no interpolated quad can cut
// through terrain. Coordinates stay in physical ENU until final GPU conversion.
class ViewshedOverlay
{
  public:
    GeoKernel3D::Viewshed result;
    std::shared_ptr<GeoKernel3D::PlacedModel> model;
    size_t next = 0, total = 0;
    ViewshedOverlay(const GeoKernel3D::Viewshed& r,
                    std::shared_ptr<const GeoKernel3D::SceneReference> ref, size_t count)
        : result(r), total(count)
    {
        model = std::make_shared<GeoKernel3D::PlacedModel>();
        model->reference = std::move(ref);
    }
    static std::vector<GeoKernel3D::Vec3d> clip(std::vector<GeoKernel3D::Vec3d> poly, int axis,
                                                double edge, bool greater)
    {
        if (poly.empty())
            return {};
        auto coordinate = [&](auto p) { return axis == 0 ? p.x : p.y; };
        auto inside = [&](auto p) {
            return greater ? coordinate(p) >= edge : coordinate(p) <= edge;
        };
        std::vector<GeoKernel3D::Vec3d> out;
        auto a = poly.back();
        bool ain = inside(a);
        for (auto b : poly)
        {
            bool bin = inside(b);
            if (ain != bin)
            {
                double t = (edge - coordinate(a)) / (coordinate(b) - coordinate(a));
                out.push_back(
                    {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)});
            }
            if (bin)
                out.push_back(b);
            a = b;
            ain = bin;
        }
        return out;
    }
    void add(const std::array<GeoKernel3D::Vec3d, 3>& triangle)
    {
        using namespace GeoKernel3D;
        const double step = result.cellSpacing, west = result.observer.x - result.radius - step / 2,
                     north = result.observer.y + result.radius + step / 2;
        double minX = triangle[0].x, maxX = minX, minY = triangle[0].y, maxY = minY;
        for (auto p : triangle)
        {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        int left = int(std::floor((minX - west) / step)),
            right = int(std::floor((maxX - west) / step));
        int top = int(std::floor((north - maxY) / step)),
            bottom = int(std::floor((north - minY) / step));
        for (int row = std::max(0, top); row <= std::min(int(result.width) - 1, bottom); ++row)
            for (int col = std::max(0, left); col <= std::min(int(result.width) - 1, right); ++col)
            {
                auto state = result.cells[size_t(row) * result.width + col];
                if (state == ViewshedCell::Outside || state == ViewshedCell::Pending)
                    continue;
                std::vector<Vec3d> poly(triangle.begin(), triangle.end());
                poly = clip(std::move(poly), 0, west + col * step, true);
                poly = clip(std::move(poly), 0, west + (col + 1) * step, false);
                poly = clip(std::move(poly), 1, north - (row + 1) * step, true);
                poly = clip(std::move(poly), 1, north - row * step, false);
                const auto color = state == ViewshedCell::Clear     ? QColor("#239b56")
                                   : state == ViewshedCell::Blocked ? QColor("#d35445")
                                                                    : QColor("#858585");
                auto linear = [](float v) {
                    return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
                };
                for (size_t i = 1; i + 1 < poly.size(); ++i)
                {
                    QVector3D p[3];
                    const Vec3d local[] = {poly[0], poly[i], poly[i + 1]};
                    for (int j = 0; j < 3; ++j)
                    {
                        auto q = local[j];
                        q.z += 0.15;
                        auto r = model->reference->toRender(q);
                        p[j] = {float(r.x), float(r.y), float(r.z)};
                    }
                    auto normal = QVector3D::crossProduct(p[1] - p[0], p[2] - p[0]);
                    if (normal.lengthSquared() < 1e-24f)
                        continue;
                    normal.normalize();
                    if (model->vertices.size() + 3 > 64ull * 1024 * 1024 / sizeof(TerrainVertex))
                        throw std::runtime_error(
                            "Terrain overlay exceeds 64 MiB. Reduce analysis radius.");
                    for (auto point : p)
                    {
                        TerrainVertex v{};
                        v.x = point.x();
                        v.y = point.y();
                        v.z = point.z();
                        v.nx = normal.x();
                        v.ny = normal.y();
                        v.nz = normal.z();
                        v.r = linear(color.redF());
                        v.g = linear(color.greenF());
                        v.b = linear(color.blueF());
                        v.material = 2;
                        v.morphY = 1;
                        model->vertices.push_back(v);
                    }
                }
            }
    }
    std::shared_ptr<const GeoKernel3D::PlacedModel> finish()
    {
        using namespace GeoKernel3D;
        if (model->vertices.empty())
            return {};
        auto mats = std::make_shared<ModelMaterials>();
        mats->atlas = QImage(32, 1, QImage::Format_RGBA8888);
        mats->atlas.fill(Qt::white);
        ModelMaterial mat;
        mat.baseColor = {1, 1, 1, .55f};
        mat.factors = {0, 1, -1, 8192 + 512 + 16};
        mats->materials.push_back(mat);
        mats->ranges.push_back({0, uint32_t(model->vertices.size()), 0});
        model->materials = mats;
        QVector3D low(model->vertices[0].x, model->vertices[0].y, model->vertices[0].z), high = low;
        for (auto v : model->vertices)
        {
            QVector3D p(v.x, v.y, v.z);
            for (int j = 0; j < 3; ++j)
            {
                low[j] = std::min(low[j], p[j]);
                high[j] = std::max(high[j], p[j]);
            }
        }
        model->center = (low + high) * .5f;
        model->radius = (high - low).length() * .5f;
        return model;
    }
};
