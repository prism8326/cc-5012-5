#include <QApplication>
#include <QTimer>
#include <QDir>
#include "MainWindow.h"

// Offscreen smoke test: instantiate the full workbench, let it shape the
// demo cue, and grab the widget tree to a PNG so the preview/editor UI can be
// inspected without a display server.
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    sub::MainWindow w;
    w.resize(1400, 900);
    w.show();

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                 : "/workspace/output/gui.png";
    QTimer::singleShot(300, &app, [&] {
        QImage img = w.grab().toImage();
        img.save(out, "PNG");
        QDir().mkpath(QFileInfo(out).absolutePath());
        img.save(out, "PNG");
        qDebug("saved %s %dx%d", qPrintable(out), img.width(), img.height());
        app.quit();
    });
    return app.exec();
}
