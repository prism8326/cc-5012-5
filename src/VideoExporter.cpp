#include "VideoExporter.h"

#include <QProcess>
#include <QPainter>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <cmath>

namespace sub {

VideoExporter::VideoExporter(FontManager *fm, QObject *parent)
    : QObject(parent), m_fm(fm) {}

bool VideoExporter::runFFmpeg(const QString &ffmpeg, const QStringList &args,
                              QString &error)
{
    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(15000)) {
        error = QStringLiteral("ffmpeg failed to start: %1").arg(proc.errorString());
        return false;
    }
    proc.waitForFinished(-1);
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        error = QString::fromLocal8Bit(proc.readAllStandardError()).right(2000);
        return false;
    }
    return true;
}

bool VideoExporter::exportProject(const Project &project,
                                  const Options &opt,
                                  QString &error)
{
    TextShaper shaper(m_fm);
    SubtitleRenderer renderer(m_fm);
    const LayoutConfig &cfg = project.layout;

    // Shape every cue up front so any missing-glyph error aborts before we
    // touch the encoder or write partial output.
    struct Prepared {
        const SubtitleCue *cue;
        ShapedParagraph para;
        ShapeDiagnostics diag;
        RenderResult rr;
    };
    QVector<Prepared> prepared;
    for (const SubtitleCue &cue : project.cues) {
        Prepared p;
        p.cue = &cue;
        p.para = shaper.shape(cue.text, cue.styleRuns, cue.forcedDir,
                              cfg, p.diag);
        if (p.diag.hasErrors()) {
            error = QStringLiteral("cue %1: %2")
                .arg(cue.id, p.diag.missing.first().reason);
            return false;
        }
        p.rr = renderer.renderFrame(p.para, cfg);
        if (!p.rr.ok) {
            error = QStringLiteral("cue %1 render blocked: %2")
                .arg(cue.id, p.rr.errors.join("; "));
            return false;
        }
        prepared.push_back(std::move(p));
    }
    if (prepared.isEmpty()) {
        error = QStringLiteral("no cues to export");
        return false;
    }

    const double totalMs = std::ceil(
        std::max_element(project.cues.begin(), project.cues.end(),
            [](const SubtitleCue &a, const SubtitleCue &b) {
                return a.endMs < b.endMs; })->endMs);
    const int totalFrames = std::max(1, int(std::ceil(totalMs / 1000.0 * opt.fps)));

    QTemporaryDir work;
    if (!work.isValid()) { error = "cannot create temp directory"; return false; }
    const QString framePattern = work.path() + "/frame_%06d.png";
    const QImage blank(int(cfg.videoWidth), int(cfg.videoHeight),
                       QImage::Format_ARGB32_Premultiplied);

    emit progress(2, QStringLiteral("rendering %1 frames").arg(totalFrames));
    for (int frame = 0; frame < totalFrames; ++frame) {
        const double tMs = frame * 1000.0 / opt.fps;
        QImage composed = blank;
        composed.fill(QColor(18, 18, 22));
        QPainter p(&composed);
        for (const Prepared &pr : prepared) {
            if (tMs >= pr.cue->startMs && tMs < pr.cue->endMs) {
                p.drawImage(0, 0, pr.rr.image);
                break; // cues do not overlap in this project model
            }
        }
        p.end();
        const QString fn = work.path() +
            QString::asprintf("/frame_%06d.png", frame + 1);
        if (!composed.save(fn, "PNG")) {
            error = "failed writing frame " + fn;
            return false;
        }
        if (frame % 25 == 0)
            emit progress(2 + int(90.0 * frame / totalFrames),
                          QStringLiteral("frame %1/%2").arg(frame).arg(totalFrames));
    }

    // Layout data next to the video, describing the exact glyph placement.
    if (!opt.layoutDataPath.isEmpty()) {
        QJsonArray cueLayouts;
        for (const Prepared &pr : prepared) {
            QRectF box = renderer.computeBox(pr.para, cfg);
            QJsonObject o = pr.cue->toJson();
            o["layout"] = ProjectStore::layoutJson(pr.para, cfg, box, pr.diag);
            cueLayouts.append(o);
        }
        QJsonObject root;
        root["fps"] = opt.fps;
        root["videoWidth"] = cfg.videoWidth;
        root["videoHeight"] = cfg.videoHeight;
        root["cues"] = cueLayouts;
        QFile lf(opt.layoutDataPath);
        if (!lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error = "cannot write layout data: " + lf.errorString();
            return false;
        }
        lf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    emit progress(94, "encoding video with ffmpeg");
    QStringList args = {"-y", "-framerate", QString::number(opt.fps),
                        "-i", framePattern,
                        "-c:v", "libx264", "-pix_fmt", "yuv420p",
                        "-movflags", "+faststart",
                        opt.outputPath};
    if (!runFFmpeg(opt.ffmpegPath, args, error)) return false;

    emit progress(100, "export complete");
    emit logMessage(QStringLiteral("wrote %1 (%2 frames)")
                        .arg(opt.outputPath).arg(totalFrames));
    return true;
}

} // namespace sub
