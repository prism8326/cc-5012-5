#include "PreviewWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QStaticText>
#include <cstring>
#include <cmath>

namespace sub {

PreviewWidget::PreviewWidget(FontManager *fm, QWidget *parent)
    : QWidget(parent), m_fm(fm)
{
    setMinimumSize(720, 420);
    setMouseTracking(false);
}

void PreviewWidget::setContent(const ShapedParagraph *para,
                               const LayoutConfig *cfg,
                               const ShapeDiagnostics *diag)
{
    m_para = para;
    m_cfg = cfg;
    m_diag = diag;
    update();
}

void PreviewWidget::setSelection(int logicalStart, int logicalEnd)
{
    if (logicalEnd < logicalStart) std::swap(logicalStart, logicalEnd);
    if (m_selStart == logicalStart && m_selEnd == logicalEnd) return;
    m_selStart = logicalStart;
    m_selEnd = logicalEnd;
    update();
}

// Map a widget-space point back to a cluster, using the same geometry the
// painter uses. Hit testing goes through the shaped cluster positions rather
// than through Qt's own text layout, so the highlight can never disagree with
// the pixels the renderer drew.
PreviewWidget::Hit PreviewWidget::hitTest(const QPointF &point) const
{
    Hit hit;
    if (!m_para || !m_cfg || m_para->lines.isEmpty()) return hit;
    const LayoutConfig &cfg = *m_cfg;
    const double sx = width() / cfg.videoWidth;
    const double sy = height() / cfg.videoHeight;
    const double vx = point.x() / sx;
    const double vy = point.y() / sy;

    SubtitleRenderer renderer(m_fm);
    const QRectF box = renderer.computeBox(*m_para, cfg);
    const double lineStep = (cfg.pointSize * 0.9 + cfg.pointSize * 0.25)
                            * cfg.lineSpacing;
    if (vy < box.top()) return hit;
    const int lineIdx = int((vy - box.top()) / lineStep);
    if (lineIdx < 0 || lineIdx >= m_para->lines.size()) return hit;
    const ShapedLine &line = m_para->lines[lineIdx];
    double x = box.left();
    if (line.baseLevel & 1) x = box.right() - line.width;
    double local = vx - x;
    int found = -1;
    for (int i = 0; i < line.glyphs.size(); ++i) {
        const ShapedGlyph &g = line.glyphs[i];
        const double end = g.xPosition + g.xAdvance;
        if (local >= g.xPosition && local < end) { found = i; break; }
        if (g.xAdvance > 0 && local < g.xPosition) { found = i; break; }
    }
    if (found < 0 && !line.glyphs.isEmpty()) found = line.glyphs.size() - 1;
    if (found < 0) return hit;
    hit.line = lineIdx;
    hit.glyph = found;
    hit.sourceIndex = line.glyphs[found].sourceIndex;
    return hit;
}

static void paintCellHighlight(QPainter &p, const ShapedGlyph &g,
                               double lineX, double baseline, double pointSize)
{
    const QRectF cell(lineX + g.xPosition, baseline - pointSize * 0.95,
                      std::max(g.xAdvance, pointSize * 0.18), pointSize * 1.2);
    p.fillRect(cell, QColor(90, 150, 255, 80));
}

void PreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 34));
    if (!m_para || !m_cfg) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, tr("No cue selected"));
        return;
    }
    const LayoutConfig &cfg = *m_cfg;
    const double sx = width() / cfg.videoWidth;
    const double sy = height() / cfg.videoHeight;
    p.save();
    p.scale(sx, sy);

    // Video plate.
    p.fillRect(QRectF(0, 0, cfg.videoWidth, cfg.videoHeight),
               QColor(18, 18, 22));

    SubtitleRenderer renderer(m_fm);
    QRectF box = renderer.computeBox(*m_para, cfg);

    // Anchor box (layout data uses identical coordinates).
    p.setPen(QPen(QColor(90, 150, 255, 180), 2, Qt::DashLine));
    p.setBrush(QColor(90, 150, 255, 28));
    p.drawRect(box);

    // Paint glyphs through the same FreeType path as export. Missing glyphs
    // are rendered as a diagnostic placeholder (red hexagon + codepoint),
    // never as a silent .notdef box.
    const double lineStep = (cfg.pointSize * 0.9 + cfg.pointSize * 0.25)
                            * cfg.lineSpacing;
    double y = box.top();
    for (const ShapedLine &line : m_para->lines) {
        double x = box.left();
        if (line.baseLevel & 1) x = box.right() - line.width;
        const double baseline = y + line.ascent;

        // Selection cells are painted before the glyphs so the text stays
        // legible on top. Logical-range -> visual cells already happened in
        // the shaper; here we only consult each glyph's source span.
        if (m_selStart >= 0 && m_selEnd > m_selStart) {
            for (const ShapedGlyph &g : line.glyphs)
                if (g.sourceIndex < m_selEnd && g.sourceEnd > m_selStart)
                    paintCellHighlight(p, g, x, baseline, cfg.pointSize);
        }

        for (const ShapedGlyph &g : line.glyphs) {
            LoadedFont *f = m_fm->byId(g.fontId);
            if (!f) continue;
            if (g.isGlyphMissing) {
                p.setPen(QPen(Qt::red, 2));
                p.setBrush(QColor(120, 0, 0, 160));
                double s = cfg.pointSize * 0.7;
                QPolygonF hex;
                for (int k = 0; k < 6; ++k) {
                    double a = k * M_PI / 3.0;
                    hex << QPointF(x + g.xPosition + s / 2 + s / 2 * std::cos(a),
                                   baseline - s / 2 + s / 2 * std::sin(a));
                }
                p.drawPolygon(hex);
                p.setPen(Qt::white);
                QChar ch = (g.sourceIndex >= 0 &&
                            g.sourceIndex < m_para->logicalText.size())
                    ? m_para->logicalText.at(g.sourceIndex) : QChar('?');
                p.drawText(QRectF(x + g.xPosition, baseline - s, s, s),
                           Qt::AlignCenter,
                           QString::asprintf("U+%04X", ch.unicode()));
                continue;
            }
            FT_Face face = f->ftFace;
            if (FT_Load_Glyph(face, g.glyphId, FT_LOAD_DEFAULT) != 0) continue;
            if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) continue;
            FT_GlyphSlot slot = face->glyph;
            QImage bmp(slot->bitmap.width, slot->bitmap.rows,
                       QImage::Format_Grayscale8);
            for (int r = 0; r < int(slot->bitmap.rows); ++r)
                std::memcpy(bmp.scanLine(r),
                            slot->bitmap.buffer + r * slot->bitmap.pitch,
                            size_t(slot->bitmap.width));
            const int left = int(std::lround(x + g.xPosition)) + slot->bitmap_left;
            const int top = int(std::lround(baseline + g.yOffset)) - slot->bitmap_top;
            QColor c = (g.bidiLevel & 1) ? QColor(255, 224, 150)
                                         : QColor(235, 235, 235);
            p.setPen(c);
            for (int r = 0; r < bmp.height(); ++r)
                for (int cidx = 0; cidx < bmp.width(); ++cidx) {
                    int cov = bmp.bits()[r * bmp.bytesPerLine() + cidx];
                    if (cov) {
                        QColor cc = c;
                        cc.setAlpha(cov);
                        p.setPen(cc);
                        p.drawPoint(left + cidx, top + r);
                    }
                }
        }
        y += lineStep;
    }

    // Direction badge.
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPointSize(16);
    p.setFont(f);
    p.drawText(QRectF(24, 18, 700, 30), Qt::AlignLeft,
               (m_para->baseLevel & 1)
                   ? tr("paragraph base: RTL (level 1)")
                   : tr("paragraph base: LTR (level 0)"));
    // Mapping anomalies are drawn over the video itself, so a broken
    // logical/visual map is visible where the glyphs are, not only in a
    // caption. The preview never hides a failure behind a plausible render.
    if (m_diag && !m_diag->warnings.isEmpty()) {
        const QRectF banner(24, 54, cfg.videoWidth - 48, 44);
        p.fillRect(banner, QColor(150, 30, 30, 200));
        p.setPen(Qt::white);
        QFont wf = p.font();
        wf.setPointSize(14);
        p.setFont(wf);
        p.drawText(banner.adjusted(12, 0, -12, 0), Qt::AlignVCenter,
                   tr("Mapping warning: %1").arg(m_diag->warnings.first()));
    }
    p.restore();

    // Logical-text strip beneath the video, in logical order with indices.
    p.fillRect(QRect(0, height() - 92, width(), 92), QColor(24, 24, 28));
    p.setPen(QColor(150, 190, 255));
    QFont lf = p.font();
    lf.setPointSize(10);
    p.setFont(lf);
    p.drawText(12, height() - 70, tr("Logical text (stored verbatim, never reversed):"));
    p.setPen(Qt::white);
    QFont mf = p.font();
    mf.setPointSize(12);
    p.setFont(mf);
    p.drawText(12, height() - 48,
               m_para->logicalText.left(120) +
               (m_para->logicalText.size() > 120 ? "…" : ""));
    p.setPen(QColor(255, 170, 90));
    p.drawText(12, height() - 24,
               tr("glyph colors: warm = RTL level, neutral = LTR level"));
}

void PreviewWidget::mousePressEvent(QMouseEvent *e)
{
    if (!m_para) return;
    const Hit h = hitTest(e->position());
    if (h.line < 0) return;
    m_dragging = true;
    // Clicking any glyph selects the whole shaping cluster it belongs to, so
    // a lam-alef ligature or a base+combining-mark pair is never split.
    int a = h.sourceIndex, b = h.sourceIndex + 1;
    for (const ShapedCluster &c : m_para->clusters)
        if (c.sourceStart <= h.sourceIndex && c.sourceEnd > h.sourceIndex) {
            a = c.sourceStart;
            b = c.sourceEnd;
            break;
        }
    m_selStart = a;
    m_selEnd = b;
    update();
    emit selectionEdited(m_selStart, m_selEnd);
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging || !m_para) return;
    const Hit h = hitTest(e->position());
    if (h.line < 0) return;
    // Extend in logical space by whole clusters: a drag over the first
    // character of a ligature or a base+mark selects the whole cluster.
    int a = std::min(m_selStart, h.sourceIndex);
    int b = std::max(m_selEnd, h.sourceIndex + 1);
    for (const ShapedCluster &c : m_para->clusters) {
        if (c.sourceStart < a && c.sourceEnd > a) a = c.sourceStart;
        if (c.sourceStart < b && c.sourceEnd > b) b = c.sourceEnd;
    }
    m_selStart = a;
    m_selEnd = b;
    update();
    emit selectionEdited(m_selStart, m_selEnd);
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

} // namespace sub
