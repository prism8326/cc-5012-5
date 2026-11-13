#include "SubtitleRenderer.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H
#include <QPainter>
#include <QtMath>
#include <algorithm>

namespace sub {

SubtitleRenderer::SubtitleRenderer(FontManager *fm) : m_fm(fm) {}

QRectF SubtitleRenderer::computeBox(const ShapedParagraph &para,
                                    const LayoutConfig &cfg) const
{
    // The box is the full available text area; each line aligns inside it by
    // its base direction. Using the max line width here would shift the right
    // anchor of shorter RTL lines away from the common margin.
    const double boxW = cfg.videoWidth - cfg.marginLeft - cfg.marginRight;
    const double h = para.totalHeight > 0 ? para.totalHeight
        : (cfg.pointSize * 1.4);
    double x = cfg.marginLeft;
    using A = LayoutConfig::Anchor;
    switch (cfg.anchor) {
    case A::BottomCenter:
    case A::TopCenter:
        x = cfg.marginLeft;
        break;
    case A::BottomRight:
        x = cfg.videoWidth - cfg.marginRight - boxW;
        break;
    case A::BottomLeft:
        x = cfg.marginLeft;
        break;
    }
    double y = 0;
    if (cfg.anchor == A::TopCenter)
        y = cfg.marginTop;
    else
        y = cfg.videoHeight - cfg.marginBottom - h;
    return QRectF(x, y, boxW, h);
}

static void blendPixel(QImage &img, int x, int y, int coverage, QColor c)
{
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return;
    if (coverage <= 0) return;
    const double a = coverage / 255.0 * (c.alphaF());
    QRgb dst = img.pixel(x, y);
    const double inv = 1.0 - a;
    const int r = int(c.red() * a + qRed(dst) * inv);
    const int g = int(c.green() * a + qGreen(dst) * inv);
    const int b = int(c.blue() * a + qBlue(dst) * inv);
    const int outA = int(a * 255 + qAlpha(dst) * inv);
    img.setPixel(x, y, qRgba(r, g, b, outA));
}

void SubtitleRenderer::drawGlyph(QImage &img, LoadedFont *f, uint glyphId,
                                 double x, double baselineY, QColor color) const
{
    FT_Face face = f->ftFace;
    if (FT_Load_Glyph(face, glyphId, FT_LOAD_DEFAULT) != 0) return;
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) return;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap &bmp = slot->bitmap;
    const int left = int(std::lround(x)) + slot->bitmap_left;
    const int top = int(std::lround(baselineY)) - slot->bitmap_top;
    for (int row = 0; row < int(bmp.rows); ++row) {
        for (int col = 0; col < int(bmp.width); ++col) {
            const unsigned cov = bmp.buffer[row * bmp.pitch + col];
            blendPixel(img, left + col, top + row, cov, color);
        }
    }
}

void SubtitleRenderer::drawOutline(QImage &img, LoadedFont *f, uint glyphId,
                                   double x, double baselineY, double radius,
                                   QColor color) const
{
    FT_Face face = f->ftFace;
    if (FT_Load_Glyph(face, glyphId, FT_LOAD_DEFAULT) != 0) return;
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) return;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap &bmp = slot->bitmap;
    const int left = int(std::lround(x)) + slot->bitmap_left;
    const int top = int(std::lround(baselineY)) - slot->bitmap_top;
    const int r = std::max(1, int(std::lround(radius)));
    for (int row = 0; row < int(bmp.rows); ++row) {
        for (int col = 0; col < int(bmp.width); ++col) {
            const unsigned cov = bmp.buffer[row * bmp.pitch + col];
            if (cov == 0) continue;
            // Stamp the coverage into a small 3x3 neighborhood so adjacent
            // glyph cells merge into a continuous stroke outline.
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    blendPixel(img, left + col + dx, top + row + dy,
                               cov, color);
        }
    }
}

RenderResult SubtitleRenderer::renderFrame(const ShapedParagraph &para,
                                           const LayoutConfig &cfg,
                                           const QColor &text,
                                           const QColor &outline) const
{
    RenderResult res;
    res.image = QImage(int(cfg.videoWidth), int(cfg.videoHeight),
                       QImage::Format_ARGB32_Premultiplied);
    res.image.fill(Qt::transparent);
    res.subtitleBox = computeBox(para, cfg);

    for (int idx : para.missingGlyphLogicals) {
        QString cp = idx < para.logicalText.size()
            ? QString::asprintf("U+%04X", para.logicalText.at(idx).unicode())
            : QStringLiteral("?");
        res.errors << QStringLiteral("missing glyph at logical index %1 (%2)")
                      .arg(idx).arg(cp);
    }
    if (!res.errors.isEmpty()) {
        res.ok = false;
        return res; // no boxes painted; caller must fix coverage or acknowledge
    }

    const double lineStep = (cfg.pointSize * 0.9 + cfg.pointSize * 0.25)
                            * cfg.lineSpacing;
    double y = res.subtitleBox.top();
    for (const ShapedLine &line : para.lines) {
        // Line alignment follows its paragraph base direction.
        double x = res.subtitleBox.left();
        if (line.baseLevel & 1) // RTL: right edge anchored
            x = res.subtitleBox.right() - line.width;
        const double baseline = y + line.ascent;
        for (const ShapedGlyph &g : line.glyphs) {
            LoadedFont *f = m_fm->byId(g.fontId);
            if (!f) continue;
            const double gx = x + g.xPosition;
            const double gy = baseline + g.yOffset;
            if (outline.alpha() > 0)
                drawOutline(res.image, f, g.glyphId, gx, gy,
                            cfg.pointSize * 0.04, outline);
            drawGlyph(res.image, f, g.glyphId, gx, gy, text);
        }
        y += lineStep;
    }
    res.ok = true;
    return res;
}

} // namespace sub
