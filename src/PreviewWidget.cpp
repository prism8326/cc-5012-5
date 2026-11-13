#include "PreviewWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QStaticText>
#include <cstring>
#include <cmath>

namespace sub {

PreviewWidget::PreviewWidget(FontManager *fm, QWidget *parent)
    : QWidget(parent), m_fm(fm)
{
    setMinimumSize(720, 420);
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

} // namespace sub
