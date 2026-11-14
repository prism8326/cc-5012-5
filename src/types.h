#ifndef SUBTITLER_TYPES_H
#define SUBTITLER_TYPES_H

#include <QString>
#include <QVector>
#include <QList>
#include <QJsonObject>
#include <QRectF>
#include <QPointF>

namespace sub {

enum class ParagraphDir { Auto, LTR, RTL };

enum class Emphasis { Normal, Bold, Italic, BoldItalic };

// One contiguous run of source characters sharing font/style attributes.
struct StyleRun {
    int start = 0;          // logical offset into paragraph text
    int length = 0;
    Emphasis emphasis = Emphasis::Normal;
    QString fontFamilyHint; // optional brand-specific face
};

// Result of the bidi pass for one character.
struct BidiInfo {
    int logicalIndex = 0;   // index in the original logical text
    QChar::Direction bidiType = QChar::DirON;
    int bidiLevel = 0;      // FriBidi embedding level; odd = visual RTL
};

// One positioned glyph. visualSlot is its cluster's slot in the visual line
// (0 = left). sourceIndex/sourceEnd describe the full shaping cluster span in
// UTF-16 logical text, so a ligature glyph is attributable to every character
// it consumed and a combining mark keeps its own grapheme cluster identity.
struct ShapedGlyph {
    uint glyphId = 0;
    int sourceIndex = -1;   // first logical UTF-16 index of the cluster
    int sourceEnd = -1;     // one past the last UTF-16 index of the cluster
    int cluster = -1;       // HarfBuzz cluster (start index of the run segment)
    int clusterId = -1;     // stable cluster identity (unit index of its start)
    int clusterUnitStart = -1; // codepoint-unit range of the cluster
    int clusterUnitEnd = -1;
    int visualSlot = -1;    // visual slot of the cluster within its line
    int bidiLevel = 0;
    double xOffset = 0;     // HarfBuzz x_offset, scaled to px
    double yOffset = 0;
    double xAdvance = 0;    // scaled advance width in px
    double xPosition = 0;   // absolute pen x on the line (px, left origin)
    double yPosition = 0;   // absolute pen baseline y (px)
    int fontId = -1;        // index into the engine's resolved font table
    bool isGlyphMissing = false; // .notdef diagnosed by coverage check
};

// A visual line after wrapping. Logical order is preserved through indices.
struct ShapedLine {
    QVector<ShapedGlyph> glyphs;       // in visual order (left -> right)
    int logicalStart = 0;              // min source index covered
    int logicalEnd = 0;                // one past max source index covered
    int baseLevel = 0;                 // resolved base level for this line
    double width = 0;                  // measured line width in px
    double ascent = 0;
    double descent = 0;
    int lineNumber = 0;
};

// A shaping piece in the logical stream, before line assignment. Pieces are
// level-run ∩ font-segment intersections; keeping them lets re-wrapping reuse
// the exact HarfBuzz output instead of re-shaping with a different width.
struct ShapedPiece {
    int unitStart = -1;
    int unitEnd = -1;      // one past the last codepoint unit
    int level = 0;
    int fontId = -1;
    int glyphStart = -1;   // index into ShapedParagraph::shapedGlyphs
    int glyphCount = 0;    // HarfBuzz emission order within the piece
};

// Stable identity and display position of one shaping cluster. id is the
// codepoint-unit index of the cluster start, so it survives re-wrapping;
// visualSlot is the left-to-right cluster ordinal inside its line.
struct ShapedCluster {
    int id = -1;
    int unitStart = -1;
    int unitEnd = -1;
    int sourceStart = -1;  // UTF-16 logical index of the first character
    int sourceEnd = -1;    // one past the last UTF-16 logical index
    int lineNumber = -1;
    int visualSlot = -1;
    int glyphStart = -1;   // index into ShapedLine::glyphs
    int glyphCount = 0;
    int bidiLevel = 0;
};

struct ShapedParagraph {
    QString logicalText;               // never reordered, never mutated
    QVector<BidiInfo> bidi;            // indexed by logical position
    QVector<ShapedLine> lines;         // visual lines
    QVector<ShapedCluster> clusters;   // cluster identity + display position
    QVector<ShapedPiece> pieces;       // level-run ∩ font segments
    QVector<ShapedGlyph> shapedGlyphs; // HarfBuzz emission order per piece
    QVector<char32_t> unitCodepoints;  // codepoint per shaping unit
    QVector<int> unitLogicalIndex;     // UTF-16 index per shaping unit
    QVector<int> unitLevels;           // resolved bidi level per shaping unit
    QVector<int> visualToLogical;      // paragraph map: visual slot -> unit
    bool lineReorderFallback = false;  // a per-line FriBidi map was invalid
    int baseLevel = 0;
    double totalHeight = 0;
    QVector<int> missingGlyphLogicals; // source indices lacking any coverage
};

// A cue as edited by the user.
struct SubtitleCue {
    QString id;
    double startMs = 0;
    double endMs = 0;
    QString text;                      // logical text, stored verbatim
    ParagraphDir forcedDir = ParagraphDir::Auto;
    QVector<StyleRun> styleRuns;

    QJsonObject toJson() const;
    static SubtitleCue fromJson(const QJsonObject &o);
};

// Direction boundary policy for neutral/weak characters.
struct DirectionPolicy {
    // Treat paired brackets as mirrored (UAX #9 bracket pairs). Off => keep
    // glyphs as-is, still reordering per levels. Recorded in layout data.
    bool mirrorBrackets = true;
    // European numbers (Latin digits) keep LTR visual order inside RTL text.
    bool europeanNumbersLtr = true;
    // Currency/percent attached to numbers behave as part of the number run.
    bool currencyWithNumber = true;
    // Neutral punctuation between two runs resolves toward paragraph base
    // (true) or toward the surrounding strong run (false, UAX default).
    bool neutralToBase = false;
    // Inline emphasis (bold/italic) opens an isolated embedding level so its
    // direction boundary is explicit.
    bool emphasisIsolation = true;

    QJsonObject toJson() const;
    static DirectionPolicy fromJson(const QJsonObject &o);
};

struct LayoutConfig {
    double videoWidth = 1920;
    double videoHeight = 1080;
    double marginLeft = 120;
    double marginRight = 120;
    double marginTop = 40;
    double marginBottom = 140;
    double pointSize = 42;             // font size in px at video resolution
    double lineSpacing = 1.25;
    enum class Anchor { BottomCenter, BottomLeft, BottomRight, TopCenter };
    Anchor anchor = Anchor::BottomCenter;
    double maxBoxWidth = 0;            // 0 => video width minus side margins
    DirectionPolicy direction;
    QStringList fontPreference;        // families tried in order per script

    QJsonObject toJson() const;
    static LayoutConfig fromJson(const QJsonObject &o);
};

struct ControlCharHit {
    int logicalIndex = 0;
    uint codepoint = 0;
    QString description;
};

} // namespace sub

#endif
