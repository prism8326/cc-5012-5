#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QPainter>
#include <QTextStream>
#include <cmath>
#include <cstdio>

#include "FontManager.h"
#include "TextShaper.h"
#include "SubtitleRenderer.h"
#include "ProjectStore.h"
#include "types.h"

using namespace sub;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const QString &name, const QString &detail = {})
{
    ++g_checks;
    if (cond) {
        QTextStream(stdout) << "PASS " << name << "\n";
    } else {
        ++g_failures;
        QTextStream(stdout) << "FAIL " << name;
        if (!detail.isEmpty()) QTextStream(stdout) << " -- " << detail;
        QTextStream(stdout) << "\n";
    }
}

struct Sample {
    QString name;
    QString text;
    ParagraphDir dir;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : "/workspace/output";
    QDir().mkpath(outDir);

    FontManager fm;
    int faces = 0;
    faces += fm.loadDirectory("/workspace/.deps/local/usr/share/fonts/truetype/noto");
    faces += fm.loadDirectory("/usr/share/fonts/truetype/dejavu");
    check(faces >= 10, "font-discovery", QString::number(faces));

    LayoutConfig cfg;
    cfg.videoWidth = 1280;
    cfg.videoHeight = 720;
    cfg.pointSize = 48;
    cfg.marginLeft = cfg.marginRight = 80;
    cfg.maxBoxWidth = cfg.videoWidth - 160;
    cfg.fontPreference = {"Noto Sans Arabic", "Noto Sans Hebrew", "DejaVu Sans"};

    const QVector<Sample> samples = {
        {"arabic-ligature",
         QStringLiteral("لا"), ParagraphDir::RTL}, // lam-alef mandatory ligature
        {"arabic-combining-mark",
         QStringLiteral("بَ"), ParagraphDir::RTL}, // ba + fatha
        {"hebrew-digits-brackets",
         QStringLiteral("שלום (123) עולם"), ParagraphDir::RTL},
        {"latin-digits-in-rtl",
         QStringLiteral("المعرض 2026 (جناح A-12)"), ParagraphDir::RTL},
        {"mixed-scripts-brand",
         QStringLiteral("EXPO 2030! مرحبا بكم — Brand™"),
         ParagraphDir::Auto},
        {"hebrew-latin-alternation",
         QStringLiteral("תערוכה EXPO ביתן 7 פתוח"), ParagraphDir::Auto},
        {"forced-ltr-arabic",
         QStringLiteral("كلمة 42"), ParagraphDir::LTR},
        {"bidi-control-visible",
         QStringLiteral("a‎b‮c"), ParagraphDir::LTR},
        {"wrapping-long-line",
         QStringLiteral("هذا نص عربي طويل جدا لاختبار عملية فصل الأسطر مع أرقام "
                        "12345 وكلمات لاتينية Mixed Script Layout Test للعرض الدولي"),
         ParagraphDir::RTL},
    };

    TextShaper shaper(&fm);
    SubtitleRenderer renderer(&fm);
    QJsonArray allLayouts;

    for (const Sample &s : samples) {
        ShapeDiagnostics diag;
        ShapedParagraph p = shaper.shape(s.text, {}, s.dir, cfg, diag);
        check(!p.lines.isEmpty(), s.name + "-produces-lines");
        check(p.logicalText == s.text, s.name + "-logical-text-unchanged");
        check(p.baseLevel == 0 || p.baseLevel == 1,
              s.name + "-paragraph-base-level-valid",
              QString::number(p.baseLevel));

        int totalGlyphs = 0;
        for (const ShapedLine &l : p.lines) totalGlyphs += l.glyphs.size();
        check(totalGlyphs > 0, s.name + "-has-glyphs",
              QString::number(totalGlyphs));

        // Every glyph must trace back to a valid source index; visual order
        // and logical indices are stored independently.
        bool indicesOk = true;
        for (const ShapedLine &l : p.lines)
            for (const ShapedGlyph &g : l.glyphs)
                if (g.sourceIndex < 0 || g.sourceIndex >= s.text.size())
                    indicesOk = false;
        check(indicesOk, s.name + "-glyph-source-indices-valid");

        // Line widths stay inside the configured box.
        bool widthOk = true;
        for (const ShapedLine &l : p.lines)
            if (l.width > cfg.maxBoxWidth + 1.0) widthOk = false;
        check(widthOk, s.name + "-lines-fit-box");

        // No silent .notdef: coverage must be complete for these samples.
        check(diag.missing.isEmpty(), s.name + "-no-missing-glyphs",
              diag.missing.isEmpty() ? QString()
                                     : diag.missing.first().reason);

        RenderResult rr = renderer.renderFrame(p, cfg);
        check(rr.ok, s.name + "-render-ok", rr.errors.join(';'));
        if (rr.ok) {
            const QString png = outDir + "/" + s.name + ".png";
            check(rr.image.save(png, "PNG"), s.name + "-png-written", png);
        }

        QJsonObject lo = ProjectStore::layoutJson(p, cfg,
                                                  renderer.computeBox(p, cfg),
                                                  diag);
        QJsonObject wrapper;
        wrapper["sample"] = s.name;
        wrapper["input"] = s.text;
        wrapper["layout"] = lo;
        allLayouts.append(wrapper);
    }

    // Bracket mirror policy must change actual shaped glyphs (UAX #9 L4),
    // not just metadata, and the policy is recorded in layout data.
    {
        ShapeDiagnostics dOn, dOff;
        LayoutConfig on = cfg, off = cfg;
        on.direction.mirrorBrackets = true;
        off.direction.mirrorBrackets = false;
        const QString t = QStringLiteral("שלום (123)");
        ShapedParagraph pOn = shaper.shape(t, {}, ParagraphDir::RTL, on, dOn);
        ShapedParagraph pOff = shaper.shape(t, {}, ParagraphDir::RTL, off, dOff);
        auto glyphAt = [](const ShapedParagraph &p, int src) {
            for (const auto &l : p.lines)
                for (const auto &g : l.glyphs)
                    if (g.sourceIndex == src) return g.glyphId;
            return uint(0);
        };
        check(glyphAt(pOn, 5) != glyphAt(pOff, 5),
              "bracket-policy-changes-glyph");
        check(glyphAt(pOn, 5) == glyphAt(pOff, 9) &&
              glyphAt(pOn, 9) == glyphAt(pOff, 5),
              "mirror-on-swaps-bracket-glyphs");
        QJsonObject lj = ProjectStore::layoutJson(pOn, on,
                                                  renderer.computeBox(pOn, on),
                                                  dOn);
        check(lj["directionPolicy"].toObject()["mirrorBrackets"].toBool(),
              "bracket-policy-recorded-in-layout");
    }

    // Ligature-specific: lam-alef shapes to fewer glyphs than codepoints.
    {
        ShapeDiagnostics d;
        ShapedParagraph p = shaper.shape(samples[0].text, {}, samples[0].dir,
                                         cfg, d);
        int glyphs = 0;
        for (const auto &l : p.lines) glyphs += l.glyphs.size();
        check(glyphs < samples[0].text.size(), "lam-alef-ligature-joins",
              QString("glyphs=%1 chars=%2").arg(glyphs).arg(samples[0].text.size()));
    }

    // Combining mark: fatha produces a separate glyph attached to the base.
    {
        ShapeDiagnostics d;
        ShapedParagraph p = shaper.shape(samples[1].text, {}, samples[1].dir,
                                         cfg, d);
        int glyphs = 0;
        for (const auto &l : p.lines) glyphs += l.glyphs.size();
        check(glyphs >= 2, "combining-fatha-renders", QString::number(glyphs));
        bool sharedBase = false;
        for (const auto &l : p.lines)
            for (const auto &g : l.glyphs)
                if (g.sourceIndex == 0) sharedBase = true;
        check(sharedBase, "combining-mark-keeps-base-index");
    }

    // Bidi control characters must appear in the visible inspection list.
    {
        ShapeDiagnostics d;
        shaper.shape(samples[7].text, {}, samples[7].dir, cfg, d);
        check(d.controls.size() >= 2, "control-chars-detected",
              QString::number(d.controls.size()));
    }

    // Reproducibility: shaping twice yields identical glyph ids, positions and
    // source indices (deterministic shaping process, no random state).
    {
        ShapeDiagnostics d1, d2;
        ShapedParagraph a = shaper.shape(samples[4].text, {}, samples[4].dir,
                                         cfg, d1);
        ShapedParagraph b = shaper.shape(samples[4].text, {}, samples[4].dir,
                                         cfg, d2);
        bool same = a.lines.size() == b.lines.size() &&
                    a.baseLevel == b.baseLevel;
        if (same)
            for (int i = 0; i < a.lines.size() && same; ++i) {
                const auto &la = a.lines[i].glyphs;
                const auto &lb = b.lines[i].glyphs;
                if (la.size() != lb.size()) { same = false; break; }
                for (int j = 0; j < la.size(); ++j)
                    if (la[j].glyphId != lb[j].glyphId ||
                        la[j].sourceIndex != lb[j].sourceIndex ||
                        std::abs(la[j].xPosition - lb[j].xPosition) > 1e-9)
                        same = false;
            }
        check(same, "reproducible-shaping");
    }

    // Inline emphasis selects the bold face for the marked run only, and the
    // level-isolation boundary keeps the surrounding bidi order intact.
    {
        SubtitleCue cue;
        cue.text = QStringLiteral("عنوان EXPO ختامي");
        StyleRun r;
        r.start = cue.text.indexOf("EXPO");
        r.length = 4;
        r.emphasis = Emphasis::Bold;
        cue.styleRuns.push_back(r);
        ShapeDiagnostics d;
        ShapedParagraph p = shaper.shape(cue.text, cue.styleRuns,
                                         ParagraphDir::Auto, cfg, d);
        check(d.missing.isEmpty(), "emphasis-no-missing-glyphs");
        bool sawBold = false, sawRegular = false;
        for (const auto &l : p.lines)
            for (const auto &g : l.glyphs) {
                LoadedFont *f = fm.byId(g.fontId);
                if (g.sourceIndex >= r.start &&
                    g.sourceIndex < r.start + r.length)
                    sawBold |= (f && f->bold);
                else
                    sawRegular |= (f && !f->bold);
            }
        check(sawBold, "emphasis-run-uses-bold-face");
        check(sawRegular, "non-emphasis-run-keeps-regular-face");
        check(p.logicalText == cue.text, "emphasis-keeps-logical-text");
    }

    // Missing glyph diagnostic: a private-use codepoint nobody covers.
    {
        ShapeDiagnostics d;
        QString t = QStringLiteral("abc") + QChar(0xF123);
        ShapedParagraph p = shaper.shape(t, {}, ParagraphDir::LTR, cfg, d);
        check(!d.missing.isEmpty(), "missing-glyph-diagnosed");
        RenderResult rr = renderer.renderFrame(p, cfg);
        check(!rr.ok && !rr.errors.isEmpty(),
              "export-blocks-on-missing-glyph");
    }

    // Re-wrap at a narrower width rebuilds lines with the same glyph set.
    {
        ShapeDiagnostics d;
        ShapedParagraph p = shaper.shape(samples[8].text, {}, samples[8].dir,
                                         cfg, d);
        int before = 0;
        for (const auto &l : p.lines) before += l.glyphs.size();
        QVector<ShapedLine> narrow = shaper.wrap(p, 420, cfg.pointSize,
                                                 cfg.lineSpacing);
        int after = 0;
        for (const auto &l : narrow) after += l.glyphs.size();
        check(before == after && narrow.size() >= p.lines.size(),
              "rewrap-preserves-glyphs",
              QString("before=%1 after=%2 lines %3->%4")
                  .arg(before).arg(after).arg(p.lines.size()).arg(narrow.size()));
    }

    QFile f(outDir + "/layout-data.json");
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(QJsonDocument(allLayouts).toJson(QJsonDocument::Indented));

    QTextStream(stdout) << "\n" << g_checks - g_failures << "/" << g_checks
                        << " checks passed\n";
    return g_failures ? 1 : 0;
}
