#include <QApplication>
#include <QCoreApplication>

#include "gui/main_window.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DracoVed");
    QCoreApplication::setApplicationName("DracoVedCpp");
    dracoved::MainWindow window;
    window.showMaximized();
    return app.exec();
}
