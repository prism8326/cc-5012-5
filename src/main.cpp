#include <QApplication>
#include <QDir>
#include "MainWindow.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ExpoSubtitles");
    QCoreApplication::setApplicationName("ShapingWorkbench");

    sub::MainWindow w;
    w.show();
    return app.exec();
}
