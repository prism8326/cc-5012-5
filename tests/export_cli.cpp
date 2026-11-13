#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QDir>

#include "FontManager.h"
#include "VideoExporter.h"
#include "ProjectStore.h"

using namespace sub;

// Headless export entry point: exercises the exact same shaping/rendering
// code path as the GUI, used by the reproducible build & test scripts.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Headless subtitle video exporter");
    parser.addHelpOption();
    QCommandLineOption outOpt({"o", "output"}, "mp4 output path", "path");
    QCommandLineOption layoutOpt({"l", "layout"}, "layout json path", "path");
    QCommandLineOption ffmpegOpt("ffmpeg", "ffmpeg binary", "path");
    QCommandLineOption fontOpt("font-dir", "font directory (repeatable)",
                               "dir");
    parser.addOption(outOpt);
    parser.addOption(layoutOpt);
    parser.addOption(ffmpegOpt);
    parser.addOption(fontOpt);
    parser.process(app);

    if (!parser.isSet(outOpt)) {
        QTextStream(stderr) << "missing --output\n";
        return 2;
    }

    FontManager fm;
    QStringList fontDirs = parser.values("font-dir");
    if (fontDirs.isEmpty()) {
        fontDirs << "/workspace/.deps/local/usr/share/fonts/truetype/noto"
                 << "/usr/share/fonts/truetype/dejavu";
    }
    int faces = 0;
    for (const QString &d : fontDirs) faces += fm.loadDirectory(d);
    QTextStream(stdout) << "loaded font faces: " << faces << "\n";

    Project project;
    project.layout.videoWidth = 1280;
    project.layout.videoHeight = 720;
    project.layout.pointSize = 44;
    project.layout.marginLeft = project.layout.marginRight = 80;
    project.layout.marginBottom = 110;
    project.layout.fontPreference = {"Noto Sans Arabic", "Noto Sans Hebrew",
                                     "DejaVu Sans"};

    struct CueDef { double start, end; QString text; ParagraphDir dir; };
    const QVector<CueDef> defs = {
        {0, 2500,
         QStringLiteral("مرحبا بكم في معرض 2026 (جناح A-12)"), ParagraphDir::RTL},
        {2500, 5000,
         QStringLiteral("ברוכים הבאים לתערוכה (ביתן 7) — EXPO 2030"),
         ParagraphDir::RTL},
        {5000, 8000,
         QStringLiteral("Mixed scripts: العربية + עברית + Latin 123 (Brand™)"),
         ParagraphDir::Auto},
        {8000, 10000,
         QStringLiteral("اﻟﻒ بَ تَ ثَ — combining marks and ligatures ﻻ"),
         ParagraphDir::RTL},
    };
    for (int i = 0; i < defs.size(); ++i) {
        SubtitleCue c;
        c.id = QStringLiteral("cue-%1").arg(i + 1);
        c.startMs = defs[i].start;
        c.endMs = defs[i].end;
        c.text = defs[i].text;
        c.forcedDir = defs[i].dir;
        project.cues.push_back(c);
    }

    VideoExporter::Options opt;
    opt.outputPath = parser.value("output");
    opt.layoutDataPath = parser.isSet("layout")
        ? parser.value("layout")
        : QFileInfo(opt.outputPath).absolutePath() + "/" +
          QFileInfo(opt.outputPath).baseName() + "_layout.json";
    opt.ffmpegPath = parser.isSet("ffmpeg")
        ? parser.value("ffmpeg")
        : qEnvironmentVariable("FFMPEG", "ffmpeg");
    opt.fps = 25;

    VideoExporter exporter(&fm);
    QObject::connect(&exporter, &VideoExporter::progress,
                     [](int p, const QString &s) {
                         QTextStream(stdout) << p << "% " << s << "\n";
                     });
    QString err;
    if (!exporter.exportProject(project, opt, err)) {
        QTextStream(stderr) << "EXPORT FAILED: " << err << "\n";
        return 1;
    }
    QTextStream(stdout) << "OK " << opt.outputPath << "\n"
                        << "layout " << opt.layoutDataPath << "\n";
    return 0;
}
