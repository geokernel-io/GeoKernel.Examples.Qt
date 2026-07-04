#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QMainWindow>
#include <QMessageBox>
#include <QToolBar>
#include <QLabel>
#include <QProgressBar>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QFileInfo>
#include <QFileInfoList>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <QElapsedTimer>
#include <QTextEdit>
#include <QTime>

#include "Viewer/GisViewer.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <array>

inline QString sampleAssetPath(const QString& relativePath)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../../assets/%1").arg(relativePath)));
}

inline QString sampleImagePath(const QString& relativePath)
{
    return sampleAssetPath(QStringLiteral("images/%1").arg(relativePath));
}

inline QString sampleDataPath(const QString& relativePath)
{
    return sampleAssetPath(QStringLiteral("data/%1").arg(relativePath));
}

inline QIcon sampleIcon(const QString& fileName)
{
    QIcon icon;
    QStringList candidates;
    candidates << fileName;

    const QFileInfo requestedFile(fileName);
    const QString suffix = requestedFile.suffix();
    if (suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0)
        candidates << QStringLiteral("%1.png").arg(requestedFile.completeBaseName());
    else if (suffix.compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0)
        candidates << QStringLiteral("%1.svg").arg(requestedFile.completeBaseName());

    for (const QString& candidate : candidates)
    {
        const QString path = sampleImagePath(candidate);
        if (!QFileInfo::exists(path))
            continue;

        for (const auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled })
        {
            icon.addFile(path, QSize(), mode, QIcon::Off);
            icon.addFile(path, QSize(), mode, QIcon::On);
        }

        if (!icon.isNull())
            break;
    }

    return icon;
}

inline bool loadLayer(GeoKernel::Viewer::GisViewer& viewer, const QString& path, QWidget* parent = nullptr)
{
    QString errorMessage;
    if (viewer.addLayerFromPath(path, &errorMessage))
        return true;

    QMessageBox::critical(
        parent,
        QStringLiteral("Layer Load"),
        QStringLiteral("Layer could not be loaded:\n%1")
            .arg(errorMessage.isEmpty() ? path : errorMessage));
    return false;
}

inline bool loadWorldLayer(GeoKernel::Viewer::GisViewer& viewer, QWidget* parent = nullptr)
{
    return loadLayer(viewer, sampleDataPath(QStringLiteral("shapefile/world_4326.shp")), parent);
}

inline QAction* addToolAction(QToolBar* toolbar, const QString& iconName, const QString& text)
{
    if (toolbar == nullptr)
        return nullptr;

    QAction* action = toolbar->addAction(sampleIcon(iconName), text);
    action->setToolTip(text);
    action->setStatusTip(text);
    return action;
}

inline void addNavigationActions(QToolBar* toolbar, GeoKernel::Viewer::GisViewer& viewer)
{
    if (toolbar == nullptr)
        return;

    QAction* zoomInAction = addToolAction(toolbar, QStringLiteral("ZoomIn.svg"), QStringLiteral("Zoom In"));
    QAction* zoomOutAction = addToolAction(toolbar, QStringLiteral("ZoomOut.svg"), QStringLiteral("Zoom Out"));
    QAction* fullExtentAction = addToolAction(toolbar, QStringLiteral("FullExtent.svg"), QStringLiteral("Full Extent"));
    toolbar->addSeparator();

    auto* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);

    QAction* zoomRectAction = addToolAction(toolbar, QStringLiteral("RectangularZoom.svg"), QStringLiteral("Zoom Rect"));
    zoomRectAction->setCheckable(true);
    toolGroup->addAction(zoomRectAction);

    QAction* panAction = addToolAction(toolbar, QStringLiteral("Pan.svg"), QStringLiteral("Pan"));
    panAction->setCheckable(true);
    toolGroup->addAction(panAction);

    QObject::connect(zoomInAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.zoomIn();
    });

    QObject::connect(zoomOutAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.zoomOut();
    });

    QObject::connect(fullExtentAction, &QAction::triggered, &viewer, &GeoKernel::Viewer::GisViewer::fullExtent);

    QObject::connect(zoomRectAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::ZoomBox);
    });

    QObject::connect(panAction, &QAction::triggered, &viewer, [&viewer]
    {
        viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::Pan);
    });

    panAction->setChecked(true);
    viewer.setActiveTool(GeoKernel::Viewer::GisViewerTool::Pan);
}

inline QToolBar* createNavigationToolbar(QMainWindow& window, GeoKernel::Viewer::GisViewer& viewer)
{
    auto* toolbar = new QToolBar(&window);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    window.addToolBar(toolbar);
    addNavigationActions(toolbar, viewer);
    return toolbar;
}
