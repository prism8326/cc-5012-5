#ifndef SUBTITLER_PREVIEWWIDGET_H
#define SUBTITLER_PREVIEWWIDGET_H

#include <QWidget>
#include "SubtitleRenderer.h"
#include "TextShaper.h"

namespace sub {

// Shows the actual shaped glyphs (via the shared SubtitleRenderer pipeline)
// together with the logical text and its bidi levels. The two views are kept
// visually distinct so the user can verify that reordering never mutates the
// stored logical string.
class PreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWidget(FontManager *fm, QWidget *parent = nullptr);

    void setContent(const ShapedParagraph *para,
                    const LayoutConfig *cfg,
                    const ShapeDiagnostics *diag);

    QSize sizeHint() const override { return {960, 540}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    FontManager *m_fm;
    const ShapedParagraph *m_para = nullptr;
    const LayoutConfig *m_cfg = nullptr;
    const ShapeDiagnostics *m_diag = nullptr;
};

} // namespace sub
#endif
