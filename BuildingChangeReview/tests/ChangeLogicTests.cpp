#include "ChangeLogic.h"
#include "GeometrySupport.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <iostream>
#include <limits>

using namespace BuildingChange;
int checks = 0;
void require(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
template<class F> void throws(F function, const char* message) {
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    require(failed,message);
}
AiRasterInferenceResult grid() {
    AiRasterInferenceResult value;
    value.width=40; value.height=40; value.bandCount=1; value.dataType=AiTensorDataType::Int32;
    value.geoTransform={432000,.5,0,4584024,0,-.5}; value.projectionWkt="EPSG:25831";
    value.data=QByteArray(40*40*sizeof(qint32),0); return value;
}
void fill(AiRasterInferenceResult& grid,int x,int y,int width,int height,qint32 id) {
    auto* data=reinterpret_cast<qint32*>(grid.data.data());
    for(int row=y;row<y+height;++row)for(int column=x;column<x+width;++column)data[row*grid.width+column]=id;
}
AiRasterInferenceResult scores(const AiRasterInferenceResult& labels,float value) {
    auto result=labels;result.dataType=AiTensorDataType::Float32;
    auto* data=reinterpret_cast<float*>(result.data.data());
    std::fill(data,data+result.width*result.height,value);return result;
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        MatchOptions options; options.minimumAreaM2=1; options.toleranceM=0;
        auto a=grid(),b=grid();
        fill(a,3,3,4,4,100);fill(b,3,3,4,4,999); // IDs are local to a date.
        fill(a,12,12,4,4,7);fill(b,23,23,4,4,8);
        auto matches=compare(a,b,options);
        require(matches.before[100].overlap()==1,"Unchanged roof must match despite different instance ID");
        require(!isCandidate(matches.before[100],matches.pixelAreaM2,options),"Unchanged roof incorrectly flagged");
        require(isCandidate(matches.before[7],matches.pixelAreaM2,options),"Removed roof not detected");
        require(isCandidate(matches.after[8],matches.pixelAreaM2,options),"New roof not detected");
        require(matches.pixelAreaM2==.25,"Area must be square metres, not pixel count");
        options.minimumAreaM2=5;
        require(!isCandidate(matches.after[8],matches.pixelAreaM2,options),"Small fragments must be excluded");
        a=grid();b=grid();fill(a,5,5,4,4,1);fill(b,7,5,4,4,2);
        options.minimumAreaM2=1;options.toleranceM=1;
        matches=compare(a,b,options);
        require(matches.before[1].overlap()==1 && matches.after[2].overlap()==1,"Tolerance did not cushion 1m offset");
        a=grid();b=grid();fill(a,0,0,4,4,1);matches=compare(a,b,options);
        require(matches.edgeInstances==1,"Cropped edge roof not counted");
        require(!isCandidate(matches.before[1],matches.pixelAreaM2,options),"Clipped roof should not be a whole-building candidate");
        b.geoTransform[0]+=.5;
        throws([&]{compare(a,b,options);},"Misaligned grids must fail before comparison");
        b=grid();b.projectionWkt="EPSG:4326";
        throws([&]{compare(a,b,options);},"Different CRS must fail");
        b=grid();b.data.chop(1);
        throws([&]{compare(a,b,options);},"Truncated label storage must fail");
        b=grid();fill(b,5,5,1,1,-1);
        throws([&]{compare(a,b,options);},"Negative instance IDs must fail");
        b=grid();
        throws([&]{compare(a,b,options,[]{return true;});},"Cancellation must stop comparison");
        options.maximumOverlap=1.5;
        throws([&]{compare(a,b,options);},"Invalid coverage threshold accepted");
        options.maximumOverlap=.1;options.toleranceM=0;
        a=grid();b=grid();fill(b,10,10,4,4,9);
        auto aScore=scores(a,.05f),bScore=scores(b,.9f);
        matches=compare(a,b,options,{},&aScore,&bScore);
        require(isCandidate(matches.after[9],matches.pixelAreaM2,options),"Strong own-date / weak other-date evidence must remain reviewable");
        aScore=scores(a,.35f); // Missing instance label must not mean missing building.
        matches=compare(a,b,options,{},&aScore,&bScore);
        require(matches.after[9].overlap()==1 && !isCandidate(matches.after[9],matches.pixelAreaM2,options),"Weak raw evidence in the other date must suppress false additions");
        aScore=scores(a,.05f);bScore=scores(b,.55f);
        matches=compare(a,b,options,{},&aScore,&bScore);
        require(!isCandidate(matches.after[9],matches.pixelAreaM2,options),"Weak own-date prediction must not become a change candidate");
        aScore=scores(a,.15f);bScore=scores(b,.9f);options.maximumOtherScore=.1;
        matches=compare(a,b,options,{},&aScore,&bScore);
        require(matches.after[9].overlap()==0 && !isCandidate(matches.after[9],matches.pixelAreaM2,options),"Mean other-date evidence must gate changes even below the weak coverage threshold");
        options.maximumOtherScore=.2;
        bScore=scores(b,.9f);bScore.geoTransform[0]+=.5;
        throws([&]{compare(a,b,options,{},&aScore,&bScore);},"Shifted raw scores must be rejected");
        bScore=scores(b,std::numeric_limits<float>::quiet_NaN());
        throws([&]{compare(a,b,options,{},&aScore,&bScore);},"Nonfinite raw scores must be rejected");
        throws([&]{compare(a,b,options,{},&aScore);},"One missing raw-score date must be rejected");
        GeometrySupport geometry;
        GeoKernel::Core::Shapes::GisShapePolygon polygon;
        polygon.fromWkt("POLYGON((432000 4584000,432010 4584000,432010 4584010,432000 4584010,432000 4584000),"
                        "(432002 4584002,432002 4584004,432004 4584004,432004 4584002,432002 4584002))");
        require(std::abs(geometry.area(polygon)-96)<.001,"Source-metre area must subtract holes");
        const auto geographic=geometry.geographicGeometry(polygon);
        require(geographic["type"].toString()=="Polygon","GeoJSON geometry must be a polygon");
        const auto coordinates=geographic["coordinates"].toArray().first().toArray().first().toArray();
        require(coordinates[0].toDouble()>2 && coordinates[0].toDouble()<3 &&
                coordinates[1].toDouble()>41 && coordinates[1].toDouble()<42,"GeoJSON must use longitude/latitude, not projected metres");
        GeoKernel::Core::Shapes::GisShapePolygon invalid;
        invalid.fromWkt("POLYGON((0 0,10 10,0 10,10 0,0 0))");
        throws([&]{geometry.area(invalid);},"Invalid edited geometry must be rejected before approval/export");
        std::cout<<checks<<" change-comparison checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
