#ifndef SUBTITLER_RENDERER_H
#define SUBTITLER_RENDERER_H

#include <QImage>
#include <QColor>

#include "FontManager.h"
#include "TextShaper.h"
#include "types.h"

namespace sub {

struct RenderResult {
    QImage image;
    bool ok = false;
    QStringList errors;            // missing glyphs etc.; export blocks on these
    QRectF subtitleBox;            // actual occupied box in video coordinates
};

// Renders shaped paragraphs with the exact same FontManager (font files,
// coverage tables, fallback order) as the on-screen preview. Glyph outlines
// come straight from FreeType; Qt is used only as the pixel surface.
class SubtitleRenderer
{
public:
    SubtitleRenderer(FontManager *fm);

    RenderResult renderFrame(const ShapedParagraph &para,
                             const LayoutConfig &cfg,
                             const QColor &text = QColor(245, 245, 245),
                             const QColor &outline = QColor(0, 0, 0)) const;

    // Geometry of the anchored subtitle box, also written to layout data.
    QRectF computeBox(const ShapedParagraph &para, const LayoutConfig &cfg) const;

private:
    void drawGlyph(QImage &img, LoadedFont *f, uint glyphId,
                   double x, double baselineY, QColor color) const;
    void drawOutline(QImage &img, LoadedFont *f, uint glyphId,
                     double x, double baselineY, double radius,
                     QColor color) const;

    FontManager *m_fm;
};

} // namespace sub
#endif
