#ifndef SUBTITLER_TEXTSHAPER_H
#define SUBTITLER_TEXTSHAPER_H

#include <QObject>
#include <QVector>
#include <QString>

#include "FontManager.h"
#include "types.h"

#include <fribidi.h>

namespace sub {

// Diagnostics produced while shaping. These are surfaced in the UI and in the
// layout data; an export is aborted when missing glyphs are present unless the
// caller explicitly acknowledges them.
struct ShapeDiagnostics {
    struct Missing {
        int logicalIndex;
        char32_t codepoint;
        QString reason;
    };
    QVector<Missing> missing;
    QVector<ControlCharHit> controls;
    QStringList warnings;

    bool hasErrors() const { return !missing.isEmpty(); }
    QJsonArray toJson() const;
};

class TextShaper
{
public:
    TextShaper(FontManager *fm);

    // Full pipeline for one paragraph. logicalText is never modified or
    // reversed: all reordering happens through FriBidi level arrays and the
    // visual index map.
    ShapedParagraph shape(const QString &logicalText,
                          const QVector<StyleRun> &styleRuns,
                          ParagraphDir forcedDir,
                          const LayoutConfig &cfg,
                          ShapeDiagnostics &diag) const;

    // Re-wrap an already bidi-resolved paragraph at a new width. The bidi
    // pass and shaping are reused; only line breaks and anchors are rebuilt.
    // Paragraph base direction is re-established per line from baseLevel.
    QVector<ShapedLine> wrap(const ShapedParagraph &base,
                             double maxWidthPx,
                             double pixelSize,
                             double lineSpacing) const;

private:
    struct Unit {                 // one Unicode codepoint, mapped to source
        char32_t cp;
        int logicalIndex;        // index into the original QString
        Emphasis emphasis;
        QString familyHint;
    };

    QVector<Unit> toUnits(const QString &text, const QVector<StyleRun> &runs) const;
    const StyleRun *runAt(const QVector<StyleRun> &runs, int idx, int len) const;
    void scanControls(const QVector<Unit> &units, ShapeDiagnostics &diag) const;

    // One contiguous sequence shaped with a single resolved font.
    struct FontSegment {
        int unitStart;
        int unitLen;
        LoadedFont *font;
        Emphasis emphasis;
    };
    QVector<FontSegment> segmentByFont(const QVector<Unit> &units,
                                       const LayoutConfig &cfg,
                                       ShapeDiagnostics &diag) const;

    struct GlyphRecord {
        uint glyphId;
        int unitIndex;           // index into Unit vector
        int logicalIndex;        // source text index (from cluster)
        int bidiLevel;
        double xOffset, yOffset, xAdvance;
        int fontId;
        bool missing;
    };

    QVector<GlyphRecord> shapeSegments(const QVector<Unit> &units,
                                       const QVector<FontSegment> &segments,
                                       const QVector<FriBidiLevel> &levels,
                                       bool mirrorBrackets,
                                       double pixelSize) const;

    ShapedGlyph toShaped(const GlyphRecord &g) const;

    // Break a fully positioned visual glyph stream into lines without ever
    // touching logical order. Break opportunities come from Unicode word
    // boundaries mapped through the source indices stored on each glyph.
    QVector<ShapedLine> wrapParagraph(const QVector<ShapedGlyph> &visualGlyphs,
                                      const QString &logicalText,
                                      double maxWidthPx,
                                      const LayoutConfig &cfg,
                                      int baseLevel) const;

    FontManager *m_fm;
};

} // namespace sub
#endif
