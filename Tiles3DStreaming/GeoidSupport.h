#pragma once
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QTextStream>
#include <QRegularExpression>
#include <stdexcept>
// Convert the official regional grid to a GDAL-readable raster without changing samples.
static QString geoidRaster(const QString& source) {
    QFile f(source); if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read geoid.");
    const QByteArray bytes=f.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=
       "1008a221282bce3b5d17d463f866a45b8011e63768fb718c48f5e126b2873ed9")
        throw std::runtime_error("Unexpected regional geoid version.");
    const int start=bytes.indexOf("\\DATA");
    const auto words=QString::fromLatin1(bytes.mid(bytes.indexOf('\n',start)+1,bytes.indexOf("\\END OF DATA")-bytes.indexOf('\n',start)-1)).split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    if(words.size()!=181*211)throw std::runtime_error("Invalid geoid sample count.");
    const QString path=QFileInfo(source).absolutePath()+"/egm08d595.asc";
    QByteArray raster;QTextStream out(&raster,QIODevice::WriteOnly);out<<"ncols 211\nnrows 181\nxllcenter 0\nyllcenter 40\ncellsize 0.016666666666666667\nNODATA_value -9999\n";
    for(int i=0;i<words.size();++i){bool ok;double n=words[i].toDouble(&ok);if(!ok||n<40||n>60)throw std::runtime_error("Invalid geoid sample.");out<<words[i]<<((i+1)%211?' ':'\n');}
    out.flush();
    auto saveChanged=[](const QString& name,const QByteArray& bytes){
        QFile current(name);if(current.open(QIODevice::ReadOnly)&&current.readAll()==bytes)return;
        current.close();QSaveFile file(name);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())
            throw std::runtime_error("Cannot save geoid raster or CRS.");
    };
    saveChanged(path,raster);
    saveChanged(QFileInfo(source).absolutePath()+"/egm08d595.prj",
        "GEOGCS[\"ETRS89\",DATUM[\"European_Terrestrial_Reference_System_1989\",SPHEROID[\"GRS 1980\",6378137,298.257222101]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");
    return path;
}
