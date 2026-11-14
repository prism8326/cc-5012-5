#include "TextShaper.h"

#include <hb.h>
#include <hb-ot.h>
#include <fribidi.h>

#include <QSet>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextBoundaryFinder>
#include <climits>
#include <cmath>
#include <algorithm>

namespace sub {

QJsonArray ShapeDiagnostics::toJson() const
{
    QJsonArray arr;
    for (const auto &m : missing) {
        QJsonObject o;
        o["logicalIndex"] = m.logicalIndex;
        o["codepoint"] = QString::asprintf("U+%04X", m.codepoint);
        o["reason"] = m.reason;
        arr.append(o);
    }
    return arr;
}

TextShaper::TextShaper(FontManager *fm) : m_fm(fm) {}

const StyleRun *TextShaper::runAt(const QVector<StyleRun> &runs, int idx, int len) const
{
    for (const StyleRun &r : runs)
        if (idx >= r.start && idx < r.start + r.length && r.start + r.length <= len)
            return &r;
    return nullptr;
}

QVector<TextShaper::Unit> TextShaper::toUnits(const QString &text,
                                              const QVector<StyleRun> &runs) const
{
    QVector<Unit> units;
    int i = 0;
    const int n = text.size();
    while (i < n) {
        char32_t cp = text.at(i).unicode();
        int consumed = 1;
        if (text.at(i).isHighSurrogate() && i + 1 < n &&
            text.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            consumed = 2;
        }
        Unit u;
        u.cp = cp;
        u.logicalIndex = i;
        u.emphasis = Emphasis::Normal;
        if (const StyleRun *r = runAt(runs, i, n)) {
            u.emphasis = r->emphasis;
            u.familyHint = r->fontFamilyHint;
        }
        units.push_back(u);
        i += consumed;
    }
    return units;
}

static QString cpName(char32_t cp) { return QString::asprintf("U+%04X", cp); }

void TextShaper::scanControls(const QVector<Unit> &units, ShapeDiagnostics &diag) const
{
    static const QHash<char32_t, QString> named = {
        {0x09, "CHARACTER TABULATION"}, {0x0A, "LINE FEED"},
        {0x0B, "LINE TABULATION"}, {0x0C, "FORM FEED"}, {0x0D, "CARRIAGE RETURN"},
        {0x85, "NEL"}, {0x2028, "LINE SEPARATOR"}, {0x2029, "PARAGRAPH SEPARATOR"},
        {0x200E, "LEFT-TO-RIGHT MARK"}, {0x200F, "RIGHT-TO-LEFT MARK"},
        {0x202A, "LRE (deprecated)"}, {0x202B, "RLE (deprecated)"},
        {0x202C, "PDF (deprecated)"}, {0x202D, "LRO (deprecated)"},
        {0x202E, "RLO (deprecated)"},
        {0x2066, "LRI"}, {0x2067, "RLI"}, {0x2068, "FSI"}, {0x2069, "PDI"},
        {0xFEFF, "ZERO WIDTH NO-BREAK SPACE / BOM"},
        {0x200B, "ZERO WIDTH SPACE"}, {0x200C, "ZWNJ"}, {0x200D, "ZWJ"},
        {0x00, "NUL"}, {0x1B, "ESCAPE"}, {0x7F, "DELETE"},
    };
    for (const Unit &u : units) {
        const char32_t cp = u.cp;
        const bool c0 = (cp < 0x20 && cp != '\t' && cp != '\n') || cp == 0x7F;
        const bool c1 = (cp >= 0x80 && cp <= 0x9F);
        const bool bidiCtrl = (cp >= 0x202A && cp <= 0x202E) ||
                              (cp >= 0x2066 && cp <= 0x2069) ||
                              cp == 0x200E || cp == 0x200F ||
                              cp == 0xFEFF || cp == 0x200B;
        if (c0 || c1 || bidiCtrl || named.contains(cp)) {
            ControlCharHit h;
            h.logicalIndex = u.logicalIndex;
            h.codepoint = cp;
            h.description = named.value(cp,
                QString::asprintf("CONTROL %s", cpName(cp).toUtf8().constData()));
            diag.controls.push_back(h);
        }
    }
}

QVector<TextShaper::FontSegment>
TextShaper::segmentByFont(const QVector<Unit> &units, const LayoutConfig &cfg,
                          ShapeDiagnostics &diag) const
{
    QVector<FontSegment> segs;
    LoadedFont *cur = nullptr;
    Emphasis curEm = Emphasis::Normal;
    int start = 0;
    QSet<int> reported;
    for (int k = 0; k < units.size(); ++k) {
        const Unit &u = units[k];
        LoadedFont *f = m_fm->resolve(u.cp, u.emphasis, u.familyHint,
                                      cfg.fontPreference);
        if (!f) {
            if (!reported.contains(u.logicalIndex)) {
                reported.insert(u.logicalIndex);
                ShapeDiagnostics::Missing m;
                m.logicalIndex = u.logicalIndex;
                m.codepoint = u.cp;
                m.reason = QStringLiteral("no loaded font covers %1").arg(cpName(u.cp));
                diag.missing.push_back(m);
            }
            f = m_fm->fonts().isEmpty() ? nullptr : m_fm->fonts().constFirst();
        }
        const bool boundary = (f != cur) || (u.emphasis != curEm) ||
                              (cur && !m_fm->covers(cur, u.cp));
        if (boundary) {
            if (cur && k > start)
                segs.push_back({start, k - start, cur, curEm});
            cur = f;
            curEm = u.emphasis;
            start = k;
        }
    }
    if (cur && start < units.size())
        segs.push_back({start, int(units.size()) - start, cur, curEm});
    return segs;
}

// Logical cluster description derived from HarfBuzz output is declared in
// TextShaper.h (ClusterDesc), so layoutLines can rebuild it without exposing
// HarfBuzz types.

void TextShaper::shapePieces(ShapedParagraph &para,
                             const QVector<Unit> &units,
                             const QVector<FontSegment> &segments,
                             const QVector<FriBidiLevel> &levels,
                             bool mirrorBrackets,
                             double pixelSize) const
{
    // Split every font segment at resolved bidi-level changes. A piece is
    // shaped with exactly one direction, so Arabic joining is activated by
    // the level of the actual characters rather than a majority vote over a
    // mixed run.
    for (const FontSegment &seg : segments) {
        if (!seg.font) continue;
        int p = seg.unitStart;
        const int segEnd = seg.unitStart + seg.unitLen;
        while (p < segEnd) {
            const int level = levels[p];
            int q = p + 1;
            while (q < segEnd && levels[q] == level) ++q;

            ShapedPiece piece;
            piece.unitStart = p;
            piece.unitEnd = q;
            piece.level = level;
            piece.fontId = seg.font->fontId;
            piece.glyphStart = para.shapedGlyphs.size();

            hb_buffer_t *buf = hb_buffer_create();
            const bool rtl = (level & 1) != 0;
            hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL
                                             : HB_DIRECTION_LTR);
            hb_codepoint_t firstCp = units[p].cp;
            hb_script_t hbScript = hb_unicode_script(
                hb_unicode_funcs_get_default(), firstCp);
            hb_buffer_set_script(buf, hbScript);
            hb_buffer_set_language(buf, hb_language_get_default());
            // Grapheme clusters: a base and its combining marks share one
            // HarfBuzz cluster, so selection and hit testing cannot land on
            // a mark detached from the character that owns it.
            hb_buffer_set_cluster_level(buf,
                HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);

            QVector<unsigned> cps;
            cps.reserve(q - p);
            for (int k = p; k < q; ++k) {
                char32_t cp = units[k].cp;
                // UAX #9 rule L4: mirror at odd levels so brackets open
                // toward their content. The policy switch is recorded.
                if (mirrorBrackets && (level & 1)) {
                    FriBidiChar mirrored = 0;
                    if (fribidi_get_mirror_char(cp, &mirrored))
                        cp = char32_t(mirrored);
                }
                cps.push_back(cp);
            }
            hb_buffer_add_codepoints(buf, cps.constData(), unsigned(cps.size()),
                                     0, unsigned(cps.size()));
            hb_font_t *font = seg.font->hbFont;
            FT_Set_Pixel_Sizes(seg.font->ftFace, 0, FT_UInt(std::lround(pixelSize)));
            hb_shape(font, buf, nullptr, 0);
            const double scale = pixelSize / seg.font->unitsPerEm;

            unsigned count = 0;
            hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
            hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);
            piece.glyphCount = int(count);
            para.shapedGlyphs.resize(para.shapedGlyphs.size() + int(count));
            for (unsigned g = 0; g < count; ++g) {
                const int clusterOffset =
                    std::min(std::max(int(info[g].cluster), 0), q - p - 1);
                const int unitIdx = p + clusterOffset;
                ShapedGlyph &sg = para.shapedGlyphs[piece.glyphStart + int(g)];
                sg = ShapedGlyph();
                sg.glyphId = info[g].codepoint;
                sg.cluster = unitIdx;
                sg.clusterId = unitIdx;
                sg.clusterUnitStart = unitIdx;
                sg.clusterUnitEnd = unitIdx + 1;
                sg.sourceIndex = units[unitIdx].logicalIndex;
                const bool wide = units[unitIdx].cp > 0xFFFF;
                sg.sourceEnd = sg.sourceIndex + (wide ? 2 : 1);
                sg.xOffset = pos[g].x_offset * scale;
                sg.yOffset = pos[g].y_offset * scale;
                sg.xAdvance = pos[g].x_advance * scale;
                sg.fontId = seg.font->fontId;
                sg.isGlyphMissing = (sg.glyphId == 0);
                sg.bidiLevel = levels[unitIdx];
            }
            hb_buffer_destroy(buf);

            para.pieces.push_back(piece);
            p = q;
        }
    }
}

QVector<TextShaper::ClusterDesc> TextShaper::buildClusters(const ShapedParagraph &para) const
{
    // Reconstruct logical cluster descriptors from the shaped pieces. A
    // cluster spans from its start unit to the next cluster start in the same
    // piece; this is what makes "logical range -> glyphs" unambiguous for
    // ligatures and combining marks.
    QVector<ClusterDesc> out;
    for (const ShapedPiece &piece : para.pieces) {
        QVector<int> starts;
        for (int g = 0; g < piece.glyphCount; ++g) {
            const int s = para.shapedGlyphs[piece.glyphStart + g].clusterUnitStart;
            if (!starts.contains(s)) starts.push_back(s);
        }
        std::sort(starts.begin(), starts.end());
        for (int i = 0; i < starts.size(); ++i) {
            ClusterDesc c;
            c.unitStart = starts[i];
            c.unitEnd = (i + 1 < starts.size()) ? starts[i + 1] : piece.unitEnd;
            c.level = piece.level;
            c.fontId = piece.fontId;
            c.sourceStart = para.unitLogicalIndex.value(c.unitStart, 0);
            const int last = c.unitEnd - 1;
            const char32_t lastCp = para.unitCodepoints.value(last, 0);
            c.sourceEnd = para.unitLogicalIndex.value(last, c.sourceStart) +
                          (lastCp > 0xFFFF ? 2 : 1);
            // Glyphs of this cluster are contiguous in HarfBuzz emission
            // order. Record the run.
            bool opened = false;
            for (int g = 0; g < piece.glyphCount; ++g) {
                const ShapedGlyph &sg = para.shapedGlyphs[piece.glyphStart + g];
                if (sg.clusterUnitStart != c.unitStart) continue;
                if (!opened) { c.glyphStart = piece.glyphStart + g; opened = true; }
                ++c.glyphCount;
                c.width += sg.xAdvance;
            }
            if (!opened) c.glyphStart = piece.glyphStart;
            out.push_back(c);
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const ClusterDesc &a, const ClusterDesc &b) {
                         return a.unitStart < b.unitStart;
                     });
    return out;
}

void TextShaper::layoutLines(ShapedParagraph &para,
                             double maxWidthPx,
                             double pixelSize,
                             double lineSpacing) const
{
    Q_UNUSED(lineSpacing);
    para.lines.clear();
    para.clusters.clear();
    const QVector<ClusterDesc> clusters = buildClusters(para);
    if (clusters.isEmpty()) return;

    const QString &logicalText = para.logicalText;
    QSet<int> boundaryAfter;
    QTextBoundaryFinder bf(QTextBoundaryFinder::Word, logicalText);
    int pos = bf.position();
    while ((pos = bf.toNextBoundary()) != -1)
        if (pos > 0 && pos < logicalText.size()) boundaryAfter.insert(pos - 1);

    const double ascent = pixelSize * 0.9;
    const double descent = pixelSize * 0.25;

    // Greedy logical-order line breaking. Breaking in logical order is what
    // keeps each line a contiguous logical range; reordering happens per line
    // afterwards, so a line's trailing space can never jump across the break.
    QVector<QPair<int, int>> ranges; // [firstCluster, onePastLastCluster)
    int start = 0;
    double width = 0;
    int lastBreak = -1;
    double lastBreakWidth = 0;
    for (int i = 0; i < clusters.size(); ++i) {
        width += clusters[i].width;
        if (width > maxWidthPx && i > start) {
            if (lastBreak >= start) {
                ranges.push_back({start, lastBreak + 1});
                start = lastBreak + 1;
                width = 0;
                for (int j = start; j <= i; ++j) width += clusters[j].width;
            } else {
                ranges.push_back({start, i}); // hard break for a long token
                start = i;
                width = clusters[i].width;
            }
            lastBreak = -1;
        }
        // A break is allowed after this cluster when the logical text has a
        // word boundary at its last character.
        if (boundaryAfter.contains(clusters[i].sourceEnd - 1)) {
            lastBreak = i;
            lastBreakWidth = width;
        }
    }
    (void)lastBreakWidth;
    if (start < clusters.size()) ranges.push_back({start, clusters.size()});

    const FriBidiParType baseDir =
        (para.baseLevel & 1) ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;

    for (int ln = 0; ln < ranges.size(); ++ln) {
        const int firstC = ranges[ln].first;
        const int endC = ranges[ln].second; // one past last cluster
        const int unitStart = clusters[firstC].unitStart;
        const int unitEnd = clusters[endC - 1].unitEnd;
        const int len = unitEnd - unitStart;
        if (len <= 0) continue;

        // Reorder this line only. FriBidi is given line-local arrays with an
        // identity map, which applies L2 and the line-end part of L1 to this
        // line's actual span. Reversing a string is never involved.
        QVector<FriBidiCharType> types(len);
        QVector<FriBidiLevel> levels(len);
        QVector<FriBidiStrIndex> map(len);
        for (int k = 0; k < len; ++k) {
            const int u = unitStart + k;
            types[k] = fribidi_get_bidi_type(para.unitCodepoints.value(u, 0));
            levels[k] = para.unitLevels.value(u, para.baseLevel);
            map[k] = k;
        }
        FriBidiLevel rr = fribidi_reorder_line(
            FRIBIDI_FLAGS_DEFAULT, types.data(), len, 0,
            baseDir, levels.data(), nullptr, map.data());
        bool valid = rr != 0;
        QVector<bool> seen(len, false);
        for (int k = 0; valid && k < len; ++k) {
            const int u = map[k];
            if (u < 0 || u >= len || seen[u]) valid = false;
            else seen[u] = true;
        }
        if (!valid)
            for (int k = 0; k < len; ++k) map[k] = k; // identity, never crash
        if (!valid) para.lineReorderFallback = true;

        // First appearance of each cluster in visual slot order defines the
        // line's cluster order. HarfBuzz already emitted each cluster's
        // glyphs left-to-right, so they are copied verbatim.
        QVector<int> clusterOfUnit(len, -1);
        for (int ci = firstC; ci < endC; ++ci)
            for (int u = clusters[ci].unitStart; u < clusters[ci].unitEnd; ++u)
                if (u >= unitStart && u < unitEnd)
                    clusterOfUnit[u - unitStart] = ci;
        QVector<int> visualClusters;
        QVector<bool> emitted(clusters.size(), false);
        visualClusters.reserve(endC - firstC);
        for (int k = 0; k < len; ++k) {
            const int ci = clusterOfUnit[map[k]];
            if (ci >= 0 && !emitted[ci]) {
                emitted[ci] = true;
                visualClusters.push_back(ci);
            }
        }
        // Units with no glyph cluster (should not occur) are skipped; any
        // cluster never seen because of degenerate mapping is appended in
        // logical order so no glyph is lost.
        for (int ci = firstC; ci < endC; ++ci)
            if (!emitted[ci]) { emitted[ci] = true; visualClusters.push_back(ci); }

        ShapedLine line;
        line.baseLevel = para.baseLevel;
        line.lineNumber = ln;
        line.ascent = ascent;
        line.descent = descent;
        int lo = INT_MAX, hi = -1;
        double pen = 0.0;
        for (int slot = 0; slot < visualClusters.size(); ++slot) {
            const int ci = visualClusters[slot];
            ShapedCluster sc;
            sc.id = clusters[ci].unitStart;
            sc.unitStart = clusters[ci].unitStart;
            sc.unitEnd = clusters[ci].unitEnd;
            sc.sourceStart = clusters[ci].sourceStart;
            sc.sourceEnd = clusters[ci].sourceEnd;
            sc.lineNumber = ln;
            sc.visualSlot = slot;
            sc.glyphStart = line.glyphs.size();
            sc.glyphCount = clusters[ci].glyphCount;
            sc.bidiLevel = clusters[ci].level;
            for (int g = 0; g < clusters[ci].glyphCount; ++g) {
                ShapedGlyph sg = para.shapedGlyphs[clusters[ci].glyphStart + g];
                // Glyphs inherit the full source span of their shaping cluster
                // (a lam-alef glyph covers both letters, a mark covers its
                // base). Identity and display position stay tied to the
                // cluster, not to a single codepoint.
                sg.sourceIndex = clusters[ci].sourceStart;
                sg.sourceEnd = clusters[ci].sourceEnd;
                sg.clusterUnitStart = clusters[ci].unitStart;
                sg.clusterUnitEnd = clusters[ci].unitEnd;
                sg.visualSlot = slot;
                sg.xPosition = pen + sg.xOffset;
                sg.yPosition = ascent;
                pen += sg.xAdvance;
                line.glyphs.push_back(sg);
            }
            lo = std::min(lo, clusters[ci].sourceStart);
            hi = std::max(hi, clusters[ci].sourceEnd);
            para.clusters.push_back(sc);
        }
        line.logicalStart = lo == INT_MAX ? 0 : lo;
        line.logicalEnd = hi < 0 ? 0 : hi;
        line.width = pen;
        para.lines.push_back(line);
    }
}

void TextShaper::validateMapping(const ShapedParagraph &para,
                                 ShapeDiagnostics &diag) const
{
    // Every glyph must point at a real source character and every cluster's
    // visual slot must be unique within its line. A failure here means the
    // mapping itself is broken, not that a font is missing. Record it as a
    // warning so the UI can surface the reason instead of silently shifting
    // a highlight. Slots are per cluster: base + mark legitimately share one.
    const int n = para.logicalText.size();
    for (const ShapedLine &line : para.lines) {
        for (const ShapedGlyph &g : line.glyphs) {
            if (g.sourceIndex < 0 || g.sourceIndex >= n ||
                g.sourceEnd <= g.sourceIndex || g.sourceEnd > n) {
                diag.warnings << QStringLiteral(
                    "glyph source span out of bounds on line %1")
                    .arg(line.lineNumber);
                return;
            }
        }
        QHash<int, int> slotCluster;
        for (const ShapedGlyph &g : line.glyphs) {
            auto it = slotCluster.constFind(g.visualSlot);
            if (it != slotCluster.constEnd() && it.value() != g.clusterId) {
                diag.warnings << QStringLiteral(
                    "two clusters share visual slot %1 on line %2")
                    .arg(g.visualSlot).arg(line.lineNumber);
                return;
            }
            slotCluster.insert(g.visualSlot, g.clusterId);
        }
    }
}

static QChar::Direction qtType(FriBidiCharType t)
{
    if (FRIBIDI_IS_RTL(t)) return QChar::DirR;
    if (FRIBIDI_IS_STRONG(t)) return QChar::DirL;
    if (FRIBIDI_IS_WEAK(t)) return QChar::DirEN;
    return QChar::DirON;
}

ShapedParagraph TextShaper::shape(const QString &logicalText,
                                  const QVector<StyleRun> &styleRuns,
                                  ParagraphDir forcedDir,
                                  const LayoutConfig &cfg,
                                  ShapeDiagnostics &diag) const
{
    ShapedParagraph para;
    para.logicalText = logicalText;
    QVector<Unit> units = toUnits(logicalText, styleRuns);
    const int n = units.size();
    scanControls(units, diag);
    if (n == 0) return para;

    para.unitCodepoints.reserve(n);
    para.unitLogicalIndex.reserve(n);
    for (const Unit &u : units) {
        para.unitCodepoints.push_back(u.cp);
        para.unitLogicalIndex.push_back(u.logicalIndex);
    }

    // ---- FriBidi: embedding levels on logical codepoints -----------------
    QVector<FriBidiChar> input(n);
    QVector<FriBidiCharType> types(n);
    QVector<FriBidiBracketType> brackets(n);
    QVector<FriBidiLevel> levels(n);
    for (int k = 0; k < n; ++k) {
        input[k] = units[k].cp;
        types[k] = fribidi_get_bidi_type(input[k]);
    }
    // Bracket pairs participate in rule N0. Without bracket types a neutral
    // "(" and ")" can resolve to different sides of a direction boundary.
    fribidi_get_bracket_types(input.constData(), n, types.constData(),
                              brackets.data());
    FriBidiParType baseDir =
        forcedDir == ParagraphDir::RTL ? FRIBIDI_PAR_RTL :
        forcedDir == ParagraphDir::LTR ? FRIBIDI_PAR_LTR : FRIBIDI_PAR_ON;
    FriBidiLevel maxPlusOne = fribidi_get_par_embedding_levels_ex(
        types.constData(), brackets.constData(), n, &baseDir, levels.data());
    if (maxPlusOne == 0)
        diag.warnings << QStringLiteral("FriBidi failed to resolve levels");
    para.baseLevel = (baseDir == FRIBIDI_PAR_RTL) ? 1 : 0;
    para.unitLevels.resize(n);
    for (int k = 0; k < n; ++k) para.unitLevels[k] = int(levels[k]);

    // Paragraph-level visual order, recorded as execution evidence. FriBidi
    // requires the index map to be pre-initialised identity: it reorders the
    // array in place and a zero-filled vector would collapse every slot. The
    // result is also validated as a permutation below.
    QVector<FriBidiStrIndex> paragraphMap(n);
    for (int k = 0; k < n; ++k) paragraphMap[k] = k;
    FriBidiLevel paragraphReorder = fribidi_reorder_line(
        FRIBIDI_FLAGS_DEFAULT, types.constData(), n, 0, baseDir,
        levels.data(), nullptr, paragraphMap.data());
    if (paragraphReorder == 0)
        diag.warnings << QStringLiteral("FriBidi reorder_line failed");
    para.visualToLogical.resize(n);
    for (int k = 0; k < n; ++k) para.visualToLogical[k] = int(paragraphMap[k]);
    {
        QVector<int> seenUnit(n, 0);
        bool perm = n > 0;
        for (int k = 0; k < n; ++k) {
            const int u = para.visualToLogical[k];
            if (u < 0 || u >= n || seenUnit[u]) { perm = false; break; }
            seenUnit[u] = 1;
        }
        if (!perm) {
            diag.warnings << QStringLiteral(
                "FriBidi map is not a permutation; visual order unreliable");
            para.visualToLogical.clear();
            for (int k = 0; k < n; ++k) para.visualToLogical.push_back(k);
        }
    }

    para.bidi.resize(logicalText.size());
    for (int k = 0; k < n; ++k) {
        BidiInfo bi;
        bi.logicalIndex = units[k].logicalIndex;
        bi.bidiType = qtType(types[k]);
        bi.bidiLevel = levels[k];
        int end = k + 1 < n ? units[k + 1].logicalIndex : logicalText.size();
        for (int s = units[k].logicalIndex; s < end; ++s) para.bidi[s] = bi;
    }

    // ---- HarfBuzz shaping per level-run ∩ font segment -------------------
    QVector<FontSegment> segments = segmentByFont(units, cfg, diag);
    shapePieces(para, units, segments, levels,
                cfg.direction.mirrorBrackets, cfg.pointSize);

    const double maxW = cfg.maxBoxWidth > 0
        ? cfg.maxBoxWidth
        : cfg.videoWidth - cfg.marginLeft - cfg.marginRight;
    layoutLines(para, maxW, cfg.pointSize, cfg.lineSpacing);

    double h = 0;
    for (const ShapedLine &l : para.lines)
        h += (l.ascent + l.descent) * cfg.lineSpacing;
    para.totalHeight = h;

    for (const ShapedGlyph &g : para.shapedGlyphs)
        if (g.isGlyphMissing && !para.missingGlyphLogicals.contains(g.sourceIndex))
            para.missingGlyphLogicals.push_back(g.sourceIndex);

    if (para.lineReorderFallback)
        diag.warnings << QStringLiteral(
            "a line's FriBidi reorder produced an invalid map; its visual "
            "order fell back to logical order");

    validateMapping(para, diag);
    return para;
}

QVector<ShapedLine> TextShaper::wrap(const ShapedParagraph &base,
                                     double maxWidthPx,
                                     double pixelSize,
                                     double lineSpacing) const
{
    ShapedParagraph copy = base;
    layoutLines(copy, maxWidthPx, pixelSize, lineSpacing);
    return copy.lines;
}

QVector<VisualRange> TextShaper::visualRangesForLogicalRange(
    const ShapedParagraph &para, int logicalStart, int logicalEnd) const
{
    QVector<VisualRange> out;
    if (logicalEnd < logicalStart) std::swap(logicalStart, logicalEnd);
    for (const ShapedLine &line : para.lines) {
        int runStart = -1, runCount = 0;
        for (int i = 0; i < line.glyphs.size(); ++i) {
            const ShapedGlyph &g = line.glyphs[i];
            // A cluster is selected when the logical range intersects its
            // full source span. This is what keeps a ligature or a base +
            // combining mark together even when only one of its characters
            // is inside the selection.
            const bool selected = g.sourceIndex < logicalEnd &&
                                  g.sourceEnd > logicalStart;
            if (selected) {
                if (runStart < 0) runStart = i;
                ++runCount;
            } else if (runStart >= 0) {
                out.push_back({line.lineNumber, runStart, runCount});
                runStart = -1;
                runCount = 0;
            }
        }
        if (runStart >= 0)
            out.push_back({line.lineNumber, runStart, runCount});
    }
    return out;
}

int TextShaper::logicalIndexAtVisualGlyph(const ShapedParagraph &para,
                                          int lineNumber, int glyphIndex) const
{
    if (lineNumber < 0 || lineNumber >= para.lines.size()) return -1;
    const ShapedLine &line = para.lines[lineNumber];
    if (glyphIndex < 0 || glyphIndex >= line.glyphs.size()) return -1;
    return line.glyphs[glyphIndex].sourceIndex;
}



} // namespace sub
