#ifndef SUBTITLER_TEXTSHAPER_H
#define SUBTITLER_TEXTSHAPER_H

#include <QObject>
#include <QVector>
#include <QString>

#include "FontManager.h"
#include "types.h"

#include <fribidi.h>

namespace sub {

// One contiguous visual run of cluster cells selected by a logical range.
// Several ranges per line are possible when a logical selection crosses a
// nested direction (e.g. an embedded LTR number inside RTL prose).
struct VisualRange {
    int lineNumber = -1;
    int firstGlyph = -1;   // index into ShapedLine::glyphs (visual order)
    int glyphCount = 0;
};

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
    // Each candidate line is reordered with FriBidi at its own logical offset,
    // so line-end boundaries follow UAX #9 L1/L2 rather than a global order.
    QVector<ShapedLine> wrap(const ShapedParagraph &base,
                             double maxWidthPx,
                             double pixelSize,
                             double lineSpacing) const;

    // Cluster-aware selection mapping. A logical UTF-16 range maps to the
    // visual cells of every cluster whose source span it intersects, split
    // into contiguous visual ranges per line.
    QVector<VisualRange> visualRangesForLogicalRange(
        const ShapedParagraph &para, int logicalStart, int logicalEnd) const;

    // Inverse hit test: the logical UTF-16 index of the cluster occupying a
    // visual glyph cell. Returns -1 when the coordinates are out of range.
    int logicalIndexAtVisualGlyph(const ShapedParagraph &para, int lineNumber,
                                  int glyphIndex) const;

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

    // One shaping cluster in logical order. Its source span is the full
    // UTF-16 range consumed by the cluster (ligature letters, base+marks).
    struct ClusterDesc {
        int unitStart = -1;
        int unitEnd = -1;      // one past the last codepoint unit
        int sourceStart = -1;
        int sourceEnd = -1;
        int level = 0;
        int fontId = -1;
        int glyphStart = -1;   // index into ShapedParagraph::shapedGlyphs
        int glyphCount = 0;
        double width = 0;
    };

    // Split font segments at resolved bidi-level changes. Each piece is
    // shaped once with one direction, so Arabic joining never depends on a
    // majority vote over mixed levels.
    void shapePieces(ShapedParagraph &para,
                     const QVector<Unit> &units,
                     const QVector<FontSegment> &segments,
                     const QVector<FriBidiLevel> &levels,
                     bool mirrorBrackets,
                     double pixelSize) const;

    QVector<ClusterDesc> buildClusters(const ShapedParagraph &para) const;

    // Break the logical cluster stream into lines and reorder each line
    // independently.
    void layoutLines(ShapedParagraph &para, double maxWidthPx,
                     double pixelSize, double lineSpacing) const;

    void validateMapping(const ShapedParagraph &para,
                         ShapeDiagnostics &diag) const;

    FontManager *m_fm;
};

} // namespace sub
#endif
