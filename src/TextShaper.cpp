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

QVector<TextShaper::GlyphRecord>
TextShaper::shapeSegments(const QVector<Unit> &units,
                          const QVector<FontSegment> &segments,
                          const QVector<FriBidiLevel> &levels,
                          bool mirrorBrackets,
                          double pixelSize) const
{
    QVector<GlyphRecord> out;
    for (const FontSegment &seg : segments) {
        if (!seg.font) continue;
        hb_buffer_t *buf = hb_buffer_create();

        // Direction follows the resolved bidi level of the segment's chars.
        // HarfBuzz only activates Arabic joining/ligatures in RTL mode; this
        // is shaping direction, never a manual string reversal.
        int odd = 0, even = 0;
        for (int k = 0; k < seg.unitLen; ++k)
            (levels[seg.unitStart + k] & 1) ? ++odd : ++even;
        const bool rtl = odd > even;
        hb_buffer_set_direction(buf, rtl ? HB_DIRECTION_RTL
                                         : HB_DIRECTION_LTR);
        // Script must be real (not COMMON) so Arabic GSUB features such as
        // the mandatory lam-alef ligature and joining forms activate.
        hb_codepoint_t firstCp = units[seg.unitStart].cp;
        hb_script_t hbScript = hb_unicode_script(hb_unicode_funcs_get_default(),
                                                 firstCp);
        hb_buffer_set_script(buf, hbScript);
        hb_buffer_set_language(buf, hb_language_get_default());

        QVector<unsigned> cps;
        cps.reserve(seg.unitLen);
        for (int k = 0; k < seg.unitLen; ++k) {
            const int globalIdx = seg.unitStart + k;
            char32_t cp = units[globalIdx].cp;
            // UAX #9 rule L4: mirrored characters at an odd level are shaped
            // with their mirrored counterpart so brackets open toward their
            // content. The policy switch makes this explicit and recorded.
            if (mirrorBrackets && (levels[globalIdx] & 1)) {
                FriBidiChar mirrored = 0;
                if (fribidi_get_mirror_char(cp, &mirrored))
                    cp = char32_t(mirrored);
            }
            cps.push_back(cp);
        }
        hb_buffer_add_codepoints(buf, cps.constData(), unsigned(seg.unitLen),
                                 0, unsigned(seg.unitLen));
        // Monotone clusters keep offsets aligned with our unit vector.
        hb_buffer_set_cluster_level(buf,
            HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

        hb_font_t *font = seg.font->hbFont;
        FT_Set_Pixel_Sizes(seg.font->ftFace, 0, FT_UInt(std::lround(pixelSize)));
        hb_shape(font, buf, nullptr, 0);
        const double scale = pixelSize / seg.font->unitsPerEm;

        unsigned count = 0;
        hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
        hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);
        for (unsigned g = 0; g < count; ++g) {
            const int absCluster = seg.unitStart + int(info[g].cluster);
            const int unitIdx = std::min(absCluster, int(units.size()) - 1);
            GlyphRecord r;
            r.glyphId = info[g].codepoint;
            r.unitIndex = unitIdx;
            r.logicalIndex = units[unitIdx].logicalIndex;
            r.xOffset = pos[g].x_offset * scale;
            r.yOffset = pos[g].y_offset * scale;
            r.xAdvance = pos[g].x_advance * scale;
            r.fontId = seg.font->fontId;
            r.missing = (r.glyphId == 0);
            out.push_back(r);
        }
        hb_buffer_destroy(buf);
    }
    return out;
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

    // ---- FriBidi: embedding levels on logical codepoints -----------------
    QVector<FriBidiChar> input(n), visualChars(n);
    QVector<FriBidiCharType> types(n);
    QVector<FriBidiLevel> levels(n);
    QVector<FriBidiStrIndex> map(n);
    for (int k = 0; k < n; ++k) {
        input[k] = units[k].cp;
        types[k] = fribidi_get_bidi_type(input[k]);
    }
    FriBidiParType baseDir =
        forcedDir == ParagraphDir::RTL ? FRIBIDI_PAR_RTL :
        forcedDir == ParagraphDir::LTR ? FRIBIDI_PAR_LTR : FRIBIDI_PAR_ON;
    // The function returns max-level + 1; the paragraph base level is read
    // from the resolved base direction (ON -> decided by first strong char).
    FriBidiLevel maxPlusOne = fribidi_get_par_embedding_levels_ex(
        types.constData(), nullptr, n, &baseDir, levels.data());
    if (maxPlusOne == 0) {
        diag.warnings << QStringLiteral("FriBidi failed to resolve levels");
    }
    const FriBidiLevel baseLevel = (baseDir == FRIBIDI_PAR_RTL) ? 1 : 0;
    para.baseLevel = baseLevel;

    fribidi_set_mirroring(cfg.direction.mirrorBrackets);
    FriBidiLevel reorderResult = fribidi_reorder_line(
        FRIBIDI_FLAGS_DEFAULT, types.constData(), n, 0,
        baseDir, levels.data(),
        visualChars.data(), map.data());
    if (reorderResult == 0)
        diag.warnings << QStringLiteral("FriBidi reorder_line failed");

    para.bidi.resize(logicalText.size());
    for (int k = 0; k < n; ++k) {
        BidiInfo bi;
        bi.logicalIndex = units[k].logicalIndex;
        bi.bidiType = qtType(types[k]);
        bi.bidiLevel = levels[k];
        int end = k + 1 < n ? units[k + 1].logicalIndex : logicalText.size();
        for (int s = units[k].logicalIndex; s < end; ++s) para.bidi[s] = bi;
    }

    // ---- HarfBuzz shaping per resolved font segment (logical order) ------
    QVector<FontSegment> segments = segmentByFont(units, cfg, diag);
    QVector<GlyphRecord> glyphs = shapeSegments(
        units, segments, levels, cfg.direction.mirrorBrackets, cfg.pointSize);
    for (GlyphRecord &g : glyphs)
        if (g.logicalIndex < para.bidi.size())
            g.bidiLevel = para.bidi[g.logicalIndex].bidiLevel;

    // ---- Visual order from the FriBidi map (no string reversal) ----------
    // visualSlotOf[unit] = position where that unit appears visually. Each
    // shaped glyph is placed once; merged ligature clusters share the slot of
    // the cluster's first unit.
    QVector<int> visualSlotOf(n, -1);
    for (int slot = 0; slot < n; ++slot)
        visualSlotOf[map[slot]] = slot;

    struct Placed {
        int slot;
        int emitOrder;
        GlyphRecord record;
    };
    QVector<Placed> placed;
    placed.reserve(glyphs.size());
    for (int gi = 0; gi < glyphs.size(); ++gi) {
        Placed pl;
        pl.slot = visualSlotOf.value(glyphs[gi].unitIndex, glyphs[gi].unitIndex);
        pl.emitOrder = gi; // HarfBuzz intra-cluster order preserved
        pl.record = glyphs[gi];
        placed.push_back(pl);
    }
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Placed &a, const Placed &b) {
                         return a.slot != b.slot ? a.slot < b.slot
                                                 : a.emitOrder < b.emitOrder;
                     });

    QVector<ShapedGlyph> all;
    double pen = 0.0;
    for (const Placed &pl : placed) {
        ShapedGlyph sg = toShaped(pl.record);
        const int unitIdx = pl.record.unitIndex;
        sg.bidiLevel = levels[unitIdx];
        sg.xPosition = pen + sg.xOffset;
        pen += sg.xAdvance;
        all.push_back(sg);
    }

    double maxW = cfg.maxBoxWidth > 0
        ? cfg.maxBoxWidth : cfg.videoWidth - cfg.marginLeft - cfg.marginRight;
    para.lines = wrapParagraph(all, logicalText, maxW, cfg, baseLevel);
    double h = 0;
    for (const ShapedLine &l : para.lines)
        h += (l.ascent + l.descent) * cfg.lineSpacing;
    para.totalHeight = h;

    for (const ShapedGlyph &g : all)
        if (g.isGlyphMissing && !para.missingGlyphLogicals.contains(g.sourceIndex))
            para.missingGlyphLogicals.push_back(g.sourceIndex);
    return para;
}

ShapedGlyph TextShaper::toShaped(const GlyphRecord &g) const
{
    ShapedGlyph sg;
    sg.glyphId = g.glyphId;
    sg.sourceIndex = g.logicalIndex;
    sg.cluster = g.logicalIndex;
    sg.bidiLevel = g.bidiLevel;
    sg.xOffset = g.xOffset;
    sg.yOffset = g.yOffset;
    sg.xAdvance = g.xAdvance;
    sg.fontId = g.fontId;
    sg.isGlyphMissing = g.missing;
    return sg;
}

QVector<ShapedLine> TextShaper::wrap(const ShapedParagraph &base,
                                     double maxWidthPx,
                                     double pixelSize,
                                     double lineSpacing) const
{
    QVector<ShapedGlyph> all;
    for (const ShapedLine &l : base.lines)
        for (const ShapedGlyph &g : l.glyphs) all.push_back(g);
    LayoutConfig cfg;
    cfg.pointSize = pixelSize;
    cfg.lineSpacing = lineSpacing;
    cfg.maxBoxWidth = maxWidthPx;
    return wrapParagraph(all, base.logicalText, maxWidthPx, cfg, base.baseLevel);
}

QVector<ShapedLine> TextShaper::wrapParagraph(const QVector<ShapedGlyph> &visual,
                                              const QString &logicalText,
                                              double maxWidthPx,
                                              const LayoutConfig &cfg,
                                              int baseLevel) const
{
    QVector<ShapedLine> lines;
    if (visual.isEmpty()) return lines;

    // Unicode word boundaries in the LOGICAL text. A glyph whose source
    // character follows a boundary may start a new line.
    QSet<int> boundaryAfter;
    QTextBoundaryFinder bf(QTextBoundaryFinder::Word, logicalText);
    int pos = bf.position();
    while ((pos = bf.toNextBoundary()) != -1)
        if (pos > 0 && pos < logicalText.size()) boundaryAfter.insert(pos - 1);

    const double ascent = cfg.pointSize * 0.9;
    const double descent = cfg.pointSize * 0.25;

    auto flush = [&](int start, int endExclusive, double width) {
        ShapedLine line;
        line.baseLevel = baseLevel;
        line.lineNumber = lines.size();
        line.ascent = ascent;
        line.descent = descent;
        double p = 0.0;
        int lo = INT_MAX, hi = -1;
        for (int i = start; i < endExclusive; ++i) {
            ShapedGlyph g = visual[i];
            g.xPosition = p + g.xOffset;
            g.yPosition = ascent;
            p += g.xAdvance;
            line.glyphs.push_back(g);
            lo = std::min(lo, g.sourceIndex);
            hi = std::max(hi, g.sourceIndex);
        }
        line.logicalStart = lo == INT_MAX ? 0 : lo;
        line.logicalEnd = hi < 0 ? 0 : hi + 1;
        line.width = width;
        lines.push_back(line);
    };

    int lineStart = 0;
    double linePen = 0.0;
    double lastBreakPen = 0.0;
    int lastBreakVisual = -1;
    for (int i = 0; i < visual.size(); ++i) {
        const ShapedGlyph &g = visual[i];
        if (linePen + g.xAdvance > maxWidthPx && lineStart < i) {
            if (lastBreakVisual > lineStart) {
                flush(lineStart, lastBreakVisual + 1, lastBreakPen);
                lineStart = lastBreakVisual + 1;
            } else {
                flush(lineStart, i, linePen); // hard break for long token
                lineStart = i;
            }
            linePen = 0.0;
            lastBreakVisual = -1;
            for (int j = lineStart; j <= i; ++j) linePen += visual[j].xAdvance;
        } else {
            linePen += g.xAdvance;
        }
        if (boundaryAfter.contains(g.sourceIndex)) {
            lastBreakVisual = i;
            lastBreakPen = linePen;
        }
    }
    flush(lineStart, visual.size(), linePen);
    return lines;
}

} // namespace sub
