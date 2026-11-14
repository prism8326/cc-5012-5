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

    // Selection is stored as a logical UTF-16 range and painted by mapping it
    // through the rendered cluster positions. The logical strip always draws
    // the backing-stored characters, so editing a mixed-direction selection
    // acts on the same characters the preview highlights.
    void setSelection(int logicalStart, int logicalEnd);
    QPair<int, int> selection() const { return {m_selStart, m_selEnd}; }

    QSize sizeHint() const override { return {960, 540}; }

signals:
    void selectionEdited(int logicalStart, int logicalEnd);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    struct Hit { int line = -1; int glyph = -1; int sourceIndex = -1; };
    Hit hitTest(const QPointF &point) const;

    FontManager *m_fm;
    const ShapedParagraph *m_para = nullptr;
    const LayoutConfig *m_cfg = nullptr;
    const ShapeDiagnostics *m_diag = nullptr;
    int m_selStart = -1;
    int m_selEnd = -1;
    bool m_dragging = false;
};

} // namespace sub
#endif
