#ifndef SUBTITLER_PROJECTSTORE_H
#define SUBTITLER_PROJECTSTORE_H

#include <QString>
#include <QVector>
#include <QJsonObject>

#include "types.h"
#include "TextShaper.h"

namespace sub {

struct Project {
    int schemaVersion = 1;
    QString name;
    // Explicit font file list: preview and export always load this exact set,
    // so a reopened project renders identically on another machine.
    QStringList fontFiles;
    QStringList fontDirectories;
    LayoutConfig layout;
    QVector<SubtitleCue> cues;
};

class ProjectStore
{
public:
    static bool save(const Project &p, const QString &path, QString *err);
    static bool load(Project &p, const QString &path, QString *err);

    // Deterministic layout dump: visual order, source indices and anchors are
    // stored separately from the logical text.
    static QJsonObject layoutJson(const ShapedParagraph &para,
                                  const LayoutConfig &cfg,
                                  const QRectF &box,
                                  const ShapeDiagnostics &diag);
};

} // namespace sub
#endif
