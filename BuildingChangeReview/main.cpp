#include "DemoWindow.h"

int main(int argc,char* argv[]) {
    QApplication app(argc,argv);
    QApplication::setApplicationName("BuildingChangeReview");
    QApplication::setWindowIcon(QIcon(":/icons/geokernel.ico"));
    DemoWindow window;window.show();return app.exec();
}
