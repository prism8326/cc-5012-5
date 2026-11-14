#include "ProjectStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>

namespace sub {

static QString jsonStr(const QJsonObject &o, const char *key,
                       const QString &def = {})
{
    return o.value(QLatin1String(key)).toString(def);
}

bool ProjectStore::save(const Project &p, const QString &path, QString *err)
{
    QJsonObject root;
    root["schemaVersion"] = p.schemaVersion;
    root["name"] = p.name;
    root["fontFiles"] = QJsonArray::fromStringList(p.fontFiles);
    root["fontDirectories"] = QJsonArray::fromStringList(p.fontDirectories);
    root["layout"] = p.layout.toJson();
    QJsonArray cues;
    for (const SubtitleCue &c : p.cues) cues.append(c.toJson());
    root["cues"] = cues;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}

bool ProjectStore::load(Project &p, const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (err) *err = pe.errorString();
        return false;
    }
    QJsonObject root = doc.object();
    p.schemaVersion = root.value("schemaVersion").toInt(1);
    p.name = jsonStr(root, "name");
    p.fontFiles.clear();
    for (const QJsonValue &v : root.value("fontFiles").toArray())
        p.fontFiles.push_back(v.toString());
    p.fontDirectories.clear();
    for (const QJsonValue &v : root.value("fontDirectories").toArray())
        p.fontDirectories.push_back(v.toString());
    p.layout = LayoutConfig::fromJson(root.value("layout").toObject());
    p.cues.clear();
    for (const QJsonValue &v : root.value("cues").toArray())
        p.cues.push_back(SubtitleCue::fromJson(v.toObject()));
    return true;
}

QJsonObject ProjectStore::layoutJson(const ShapedParagraph &para,
                                     const LayoutConfig &cfg,
                                     const QRectF &box,
                                     const ShapeDiagnostics &diag)
{
    QJsonObject out;
    out["baseDirection"] = (para.baseLevel & 1) ? "rtl" : "ltr";
    out["baseLevel"] = para.baseLevel;
    out["totalHeight"] = para.totalHeight;

    QJsonObject boxO;
    boxO["x"] = box.x();
    boxO["y"] = box.y();
    boxO["width"] = box.width();
    boxO["height"] = box.height();
    out["anchoredBox"] = boxO;

    QJsonObject policy = cfg.direction.toJson();
    out["directionPolicy"] = policy;

    QJsonArray bidi;
    for (int i = 0; i < para.bidi.size(); ++i) {
        const BidiInfo &b = para.bidi[i];
        QJsonObject bo;
        bo["logicalIndex"] = i;
        bo["level"] = b.bidiLevel;
        bo["visualSide"] = (b.bidiLevel & 1) ? "right-to-left" : "left-to-right";
        bidi.append(bo);
    }
    out["bidiLevels"] = bidi;

    QJsonArray lines;
    for (const ShapedLine &l : para.lines) {
        QJsonObject lo;
        lo["lineNumber"] = l.lineNumber;
        lo["baseLevel"] = l.baseLevel;
        lo["logicalStart"] = l.logicalStart;
        lo["logicalEnd"] = l.logicalEnd;
        lo["width"] = l.width;
        QJsonArray clusters;
        for (const ShapedCluster &c : para.clusters) {
            if (c.lineNumber != l.lineNumber) continue;
            QJsonObject co;
            co["clusterId"] = c.id;
            co["sourceStart"] = c.sourceStart;
            co["sourceEnd"] = c.sourceEnd;
            co["visualSlot"] = c.visualSlot;
            co["bidiLevel"] = c.bidiLevel;
            co["glyphStart"] = c.glyphStart;
            co["glyphCount"] = c.glyphCount;
            clusters.append(co);
        }
        lo["clusters"] = clusters;
        QJsonArray glyphs;
        for (const ShapedGlyph &g : l.glyphs) {
            QJsonObject go;
            go["glyphId"] = int(g.glyphId);
            go["clusterId"] = g.clusterId;
            go["sourceStart"] = g.sourceIndex;
            go["sourceEnd"] = g.sourceEnd;
            go["visualSlot"] = g.visualSlot;
            go["bidiLevel"] = g.bidiLevel;
            go["x"] = g.xPosition;
            go["y"] = g.yPosition;
            go["advance"] = g.xAdvance;
            go["fontId"] = g.fontId;
            go["missing"] = g.isGlyphMissing;
            glyphs.append(go);
        }
        lo["glyphs"] = glyphs;
        lines.append(lo);
    }
    out["lines"] = lines;
    out["missingGlyphs"] = diag.toJson();

    QJsonObject mapping;
    mapping["mappingVersion"] = 2;
    mapping["clusterLevel"] = "grapheme";
    mapping["bidiBaseLevel"] = para.baseLevel;
    mapping["clusterCount"] = para.clusters.size();
    mapping["warnings"] = QJsonArray::fromStringList(diag.warnings);
    out["logicalVisualMapping"] = mapping;

    QJsonArray controls;
    for (const ControlCharHit &c : diag.controls) {
        QJsonObject co;
        co["logicalIndex"] = c.logicalIndex;
        co["codepoint"] = QString::asprintf("U+%04X", c.codepoint);
        co["description"] = c.description;
        controls.append(co);
    }
    out["controlCharacters"] = controls;
    out["directionPolicyApplied"] = cfg.direction.toJson();
    return out;
}

} // namespace sub
