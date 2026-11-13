#ifndef SUBTITLER_VIDEOEXPORTER_H
#define SUBTITLER_VIDEOEXPORTER_H

#include <QObject>
#include <QString>

#include "FontManager.h"
#include "TextShaper.h"
#include "SubtitleRenderer.h"
#include "ProjectStore.h"

namespace sub {

class VideoExporter : public QObject
{
    Q_OBJECT
public:
    VideoExporter(FontManager *fm, QObject *parent = nullptr);

    struct Options {
        QString ffmpegPath;
        QString outputPath;          // .mp4 / .mov / .mkv
        QString layoutDataPath;      // .json written next to the video
        int fps = 25;
        bool burnSubtitles = true;   // render text onto the frames
        // Empty background => solid dark plate; set a source video to overlay.
        QString backgroundVideo;
    };

    // Full export. Blocks; progress is emitted. Missing glyphs abort the
    // export with an error instead of producing .notdef boxes.
    bool exportProject(const Project &project,
                       const Options &opt,
                       QString &error);

signals:
    void progress(int percent, const QString &stage);
    void logMessage(const QString &msg);

private:
    bool runFFmpeg(const QString &ffmpeg, const QStringList &args,
                   QString &error);
    FontManager *m_fm;
};

} // namespace sub
#endif
