#include <QApplication>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QLabel>
#include <QTextCursor>
#include <QMouseEvent>
#include <QTextStream>
#include "MainWindow.h"
#include "PreviewWidget.h"

using namespace sub;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const QString &name, const QString &detail = {})
{
    ++g_checks;
    QTextStream(stdout) << (cond ? "PASS " : "FAIL ") << name;
    if (!detail.isEmpty()) QTextStream(stdout) << " -- " << detail;
    QTextStream(stdout) << "\n";
    if (!cond) ++g_failures;
}

// Click the glyph cell that the shaper computed for a given logical index.
// Coordinates go through the same box/anchor/scale math as the painter, so a
// pass here means the editable preview maps clicks to the stored logical
// characters through the patched cluster mapping -- not through Qt's own text
// layout.
static void clickLogical(PreviewWidget *preview, FontManager *fm,
                         const ShapedParagraph &para, const LayoutConfig &cfg,
                         int logicalIndex, int expectStart, int expectEnd,
                         QPlainTextEdit *editor)
{
    const ShapedGlyph *target = nullptr;
    const ShapedLine *targetLine = nullptr;
    for (const ShapedLine &l : para.lines)
        for (const ShapedGlyph &g : l.glyphs)
            if (g.sourceIndex == logicalIndex) { target = &g; targetLine = &l; }
    check(target != nullptr, "gui-glyph-for-logical-index-found",
          QString::number(logicalIndex));
    if (!target) return;

    SubtitleRenderer renderer(fm);
    const QRectF box = renderer.computeBox(para, cfg);
    double lineX = box.left();
    if (targetLine->baseLevel & 1) lineX = box.right() - targetLine->width;
    const double lineStep = (cfg.pointSize * 0.9 + cfg.pointSize * 0.25)
                            * cfg.lineSpacing;
    const double vx = lineX + target->xPosition + target->xAdvance * 0.5;
    const double vy = box.top() + targetLine->lineNumber * lineStep
                      + targetLine->ascent * 0.5;
    const double sx = preview->width() / cfg.videoWidth;
    const double sy = preview->height() / cfg.videoHeight;
    const QPointF pt(vx * sx, vy * sy);

    QMouseEvent press(QEvent::MouseButtonPress, pt,
                      preview->mapToGlobal(pt.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(preview, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pt,
                        preview->mapToGlobal(pt.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(preview, &release);
    QCoreApplication::processEvents();

    const QString selected = editor->textCursor().selectedText();
    check(selected == para.logicalText.mid(expectStart, expectEnd - expectStart),
          "gui-click-selects-mapped-character",
          QString("clicked logical %1 selected [%2]").arg(logicalIndex)
              .arg(selected));
    check(preview->selection() == qMakePair(expectStart, expectEnd),
          "gui-preview-selection-matches-logical",
          QString("%1..%2").arg(preview->selection().first)
              .arg(preview->selection().second));
}

// Offscreen smoke test: instantiate the full workbench, let it shape the
// demo cue, drive the real editor/preview with mixed bidi text, and grab the
// widget tree to PNGs so the logical-visual mapping can be inspected without
// a display server.
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    MainWindow w;
    w.resize(1400, 900);
    w.show();

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                 : "/workspace/output/gui.png";
    QDir().mkpath(QFileInfo(out).absolutePath());

    QTimer::singleShot(300, &app, [&] {
        QPlainTextEdit *editor = w.findChild<QPlainTextEdit *>();
        PreviewWidget *preview = w.findChild<PreviewWidget *>();
        check(editor != nullptr, "gui-editor-present");
        check(preview != nullptr, "gui-preview-present");
        if (!editor || !preview) { app.quit(); return; }

        // The initial demo cue must open with meaningful mixed text.
        check(editor->toPlainText().contains(QStringLiteral("2026")),
              "gui-demo-cue-has-mixed-text");

        // Edit a short case where logical and visual order differ: Arabic
        // word, Latin digits, a bracket pair and a nested LTR brand token.
        const QString bidiText = QStringLiteral("المعرض 2026 (جناح A-12)");
        editor->setPlainText(bidiText);
        QCoreApplication::processEvents();
        check(editor->toPlainText() == bidiText,
              "gui-editor-stores-logical-text");

        // The diagnostics strip is populated from the patched computation and
        // must flag mapping anomalies instead of silently accepting them.
        QString diagText;
        for (QLabel *l : w.findChildren<QLabel *>())
            if (l->text().contains("base level")) diagText = l->text();
        check(diagText.contains("base level 1"), "gui-shows-computed-base-level",
              diagText);
        check(!diagText.contains("warnings"),
              "gui-no-mapping-warnings", diagText);

        // Recompute the same paragraph the preview holds, then drive a click
        // on the actual glyph of logical index 10 ('6').
        FontManager fm;
        fm.loadDirectory(
            "/workspace/.deps/local/usr/share/fonts/truetype/noto");
        fm.loadDirectory("/usr/share/fonts/truetype/dejavu");
        LayoutConfig cfg;
        cfg.pointSize = 42;
        cfg.maxBoxWidth = 1680;
        cfg.marginLeft = cfg.marginRight = 120;
        cfg.fontPreference = {"Noto Sans Arabic", "Noto Sans Hebrew",
                              "DejaVu Sans"};
        ShapeDiagnostics diag;
        TextShaper shaper(&fm);
        ShapedParagraph para =
            shaper.shape(bidiText, {}, ParagraphDir::Auto, cfg, diag);
        check(diag.warnings.isEmpty(), "gui-shaping-no-warnings",
              diag.warnings.join("; "));

        clickLogical(preview, &fm, para, cfg, 10, 10, 11, editor);
        w.grab().toImage().save(out, "PNG");

        // A click on a lam-alef ligature must select the whole cluster, not
        // just the letter under the cursor.
        const QString ligText = QStringLiteral("لا");
        editor->setPlainText(ligText);
        QCoreApplication::processEvents();
        ShapeDiagnostics ligDiag;
        ShapedParagraph ligPara =
            shaper.shape(ligText, {}, ParagraphDir::Auto, cfg, ligDiag);
        if (!ligPara.lines.isEmpty() && !ligPara.lines.first().glyphs.isEmpty()) {
            const ShapedGlyph &first = ligPara.lines.first().glyphs.first();
            clickLogical(preview, &fm, ligPara, cfg, first.sourceIndex,
                         0, 2, editor);
            check(preview->selection() == qMakePair(0, 2),
                  "gui-click-selects-whole-ligature-cluster",
                  QString("%1..%2").arg(preview->selection().first)
                      .arg(preview->selection().second));
        } else {
            check(false, "gui-click-selects-whole-ligature-cluster",
                  "no ligature glyph");
        }

        // Restore the mixed sample for the editor-selection check.
        editor->setPlainText(bidiText);
        QCoreApplication::processEvents();

        // Logical selection in the editor must come back as visual cells:
        // "2026" occupies logical 7..11 and highlights four digit cells in
        // visual left-to-right order.
        QTextCursor cur = editor->textCursor();
        cur.setPosition(7);
        cur.setPosition(11, QTextCursor::KeepAnchor);
        editor->setTextCursor(cur);
        QCoreApplication::processEvents();
        check(preview->selection() == qMakePair(7, 11),
              "gui-editor-selection-propagates",
              QString("%1..%2").arg(preview->selection().first)
                  .arg(preview->selection().second));
        QVector<VisualRange> ranges =
            shaper.visualRangesForLogicalRange(para, 7, 11);
        int glyphs = 0;
        for (const VisualRange &r : ranges) glyphs += r.glyphCount;
        check(glyphs == 4, "gui-digit-selection-covers-four-glyphs",
              QString::number(glyphs));

        const QString selectedShot =
            QFileInfo(out).absolutePath() + "/gui-selection.png";
        w.grab().toImage().save(selectedShot, "PNG");

        // Mapping warnings are painted on the video plate, not only in the
        // caption: a broken logical/visual map cannot hide behind a
        // plausible-looking render. Done last because it overrides the
        // preview's borrowed pointers.
        {
            static ShapedParagraph warnedPara;
            warnedPara = para;
            ShapeDiagnostics warned = diag;
            warned.warnings << QStringLiteral("synthetic mapping warning for banner test");
            preview->setContent(&warnedPara, &cfg, &warned);
            QImage bad = w.grab().toImage();
            const QString bannerShot =
                QFileInfo(out).absolutePath() + "/gui-warning-banner.png";
            check(bad.save(bannerShot, "PNG"), "gui-warning-banner-shot",
                  bannerShot);
        }

        QTextStream(stdout) << "\n" << g_checks - g_failures << "/" << g_checks
                            << " GUI checks passed\n";
        app.quit();
    });
    const int rc = app.exec();
    return (g_failures || rc) ? 1 : 0;
}
