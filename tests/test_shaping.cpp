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

    // ---- Logical/visual mapping regressions ------------------------------
    // These pin the mapping itself, not just that "some glyphs" exist. They
    // are what fails if visual slots collapse, a cluster's source span is
    // wrong, or a wrapped line reorders across its own break.

    // (a) The FriBidi map must be a permutation and match the paragraph's
    // visual order. A zero-initialised map used to collapse every slot onto
    // unit 0 while still returning glyphs, so "renders" alone proved nothing.
    {
        ShapeDiagnostics d;
        ShapedParagraph p = shaper.shape(
            QStringLiteral("שלום (123) עולם"), {}, ParagraphDir::RTL, cfg, d);
        check(p.visualToLogical.size() == p.unitCodepoints.size(),
              "bidi-map-size-matches-units");
        QVector<int> seen(p.visualToLogical.size(), 0);
        bool perm = p.visualToLogical.size() > 0;
        for (int slot = 0; slot < p.visualToLogical.size(); ++slot) {
            const int u = p.visualToLogical[slot];
            if (u < 0 || u >= seen.size() || seen[u]) { perm = false; break; }
            seen[u] = 1;
        }
        check(perm, "bidi-map-is-permutation");
        check(d.warnings.isEmpty(), "bidi-map-no-warnings",
              d.warnings.join("; "));
        // Logical "שלום" occupies indices 0..3; in an RTL paragraph it must
        // appear right of the neutral run, i.e. at the last visual slots.
        const int lastSlot = p.visualToLogical.size() - 1;
        check(p.visualToLogical[lastSlot] == 0,
              "rtl-first-char-is-rightmost",
              QString("slot %1 -> unit %2").arg(lastSlot)
                  .arg(p.visualToLogical.value(lastSlot, -1)));
    }

    // (b) Each cluster's source span is exact and covers every logical
    // character exactly once; glyph slots are unique per cluster.
    {
        const QStringList texts = {
            QStringLiteral("שלום (123) עולם"),
            QStringLiteral("المعرض 2026 (جناح A-12)"),
            QStringLiteral("بَ"),        // base + combining mark
            QStringLiteral("لا"),        // lam-alef ligature
            QStringLiteral("EXPO 2030! مرحبا بكم — Brand™"),
        };
        bool allExact = true, allSlotsOk = true, allGlyphSpansOk = true;
        QString detail;
        for (const QString &t : texts) {
            ShapeDiagnostics d;
            ShapedParagraph p = shaper.shape(t, {}, ParagraphDir::Auto, cfg, d);
            QVector<int> cover(t.size(), 0);
            for (const ShapedCluster &c : p.clusters) {
                if (c.sourceStart < 0 || c.sourceEnd > t.size() ||
                    c.sourceEnd <= c.sourceStart) {
                    allExact = false;
                    detail += QStringLiteral("[bad span %1,%2]")
                                  .arg(c.sourceStart).arg(c.sourceEnd);
                    continue;
                }
                for (int i = c.sourceStart; i < c.sourceEnd; ++i) ++cover[i];
            }
            for (int i = 0; i < cover.size(); ++i)
                if (cover[i] != 1) {
                    allExact = false;
                    detail += QStringLiteral("[%1 index %2 cover %3]")
                                  .arg(t.left(6)).arg(i).arg(cover[i]);
                }
            for (const ShapedLine &l : p.lines) {
                QHash<int, int> slotToCluster;
                for (const ShapedGlyph &g : l.glyphs) {
                    if (g.sourceIndex < 0 || g.sourceEnd <= g.sourceIndex ||
                        g.sourceEnd > t.size())
                        allGlyphSpansOk = false;
                    auto it = slotToCluster.constFind(g.visualSlot);
                    if (it != slotToCluster.constEnd() &&
                        it.value() != g.clusterId)
                        allSlotsOk = false;
                    slotToCluster.insert(g.visualSlot, g.clusterId);
                }
            }
        }
        check(allExact, "cluster-source-spans-cover-logical-text", detail);
        check(allSlotsOk, "cluster-visual-slots-unique-per-line");
        check(allGlyphSpansOk, "glyph-source-spans-valid");
    }

    // (c) Every cluster carries a stable identity (its start unit) and a
    // display position (line + visual slot); both are needed to reconcile
    // highlight and hit-test across re-wrapping.
    {
        ShapeDiagnostics d;
        ShapedParagraph p = shaper.shape(
            QStringLiteral("ביתן 7 פתוח EXPO"), {}, ParagraphDir::RTL, cfg, d);
        bool idsUnique = true, slotsSequential = true;
        QHash<int, int> slotOf;
        for (const ShapedCluster &c : p.clusters) {
            if (slotOf.contains(c.visualSlot)) idsUnique = false;
            slotOf.insert(c.visualSlot, c.id);
            if (c.id != c.unitStart) slotsSequential = false;
        }
        check(!p.clusters.isEmpty() && idsUnique, "cluster-identities-unique");
        check(slotsSequential, "cluster-id-is-start-unit");
        // Re-wrap at a narrower width; ids must be unchanged and every id
        // must still point at the same source span.
        QVector<ShapedLine> narrow = shaper.wrap(p, 260, cfg.pointSize,
                                                 cfg.lineSpacing);
        ShapedParagraph rewrapped = p;
        rewrapped.lines = narrow;
        bool identityStable = !narrow.isEmpty();
        for (const ShapedLine &l : narrow)
            for (const ShapedGlyph &g : l.glyphs)
                if (g.sourceIndex < 0 || g.sourceEnd > p.logicalText.size())
                    identityStable = false;
        check(identityStable, "rewrap-keeps-cluster-source-spans");
    }

    // (d) Line-end boundary: a wrapped RTL line must not pull the next
    // logical word to its left edge, and the first/last logical characters
    // must land on the expected visual side of their own line.
    {
        ShapeDiagnostics d;
        const QString t = QStringLiteral(
            "هذا نص عربي طويل جدا لاختبار فصل الأسطر مع أرقام 12345");
        LayoutConfig narrowCfg = cfg;
        narrowCfg.maxBoxWidth = 420;
        ShapedParagraph p = shaper.shape(t, {}, ParagraphDir::RTL, narrowCfg, d);
        check(p.lines.size() > 1, "line-boundary-produces-multiple-lines",
              QString::number(p.lines.size()));
        bool linesContiguous = true, spansValid = true;
        int previousEnd = -1;
        for (const ShapedLine &l : p.lines) {
            if (l.logicalStart < 0 || l.logicalEnd > t.size() ||
                l.logicalEnd <= l.logicalStart)
                spansValid = false;
            if (previousEnd > l.logicalStart) linesContiguous = false;
            previousEnd = l.logicalEnd;
        }
        check(spansValid, "line-logical-spans-valid");
        check(linesContiguous, "line-logical-ranges-non-overlapping");
        // For an RTL line, visual slot 0 (leftmost) holds the last logical
        // cluster of that line. Verify on the longest line so it has >1
        // cluster and the direction claim is meaningful.
        const ShapedLine *longest = nullptr;
        for (const ShapedLine &l : p.lines)
            if (!longest || l.glyphs.size() > longest->glyphs.size())
                longest = &l;
        if (longest && longest->glyphs.size() > 1 && (longest->baseLevel & 1)) {
            const int leftmostSrc = longest->glyphs.first().sourceIndex;
            const int rightmostSrc = longest->glyphs.last().sourceIndex;
            check(leftmostSrc > rightmostSrc,
                  "rtl-line-leftmost-is-logically-last",
                  QString("left src %1 right src %2")
                      .arg(leftmostSrc).arg(rightmostSrc));
        } else {
            check(false, "rtl-line-leftmost-is-logically-last",
                  "no multi-cluster RTL line to verify");
        }
    }

    // (e) Selection mapping is cluster-aware. Selecting one letter of a
    // lam-alef ligature, or just the combining mark, must select the whole
    // visual cluster; selecting embedded digits highlights the digit glyphs
    // in visual (left-to-right) order, not reversed logical order.
    {
        ShapeDiagnostics d;
        ShapedParagraph lig = shaper.shape(QStringLiteral("لا"), {},
                                            ParagraphDir::RTL, cfg, d);
        QVector<VisualRange> rLig =
            shaper.visualRangesForLogicalRange(lig, 1, 2);
        int ligGlyphs = 0;
        for (const VisualRange &r : rLig) ligGlyphs += r.glyphCount;
        check(!rLig.isEmpty() && ligGlyphs == lig.shapedGlyphs.size(),
              "selection-keeps-ligature-together",
              QString("ranges=%1 glyphs=%2").arg(rLig.size()).arg(ligGlyphs));

        ShapedParagraph mark = shaper.shape(QStringLiteral("بَ"), {},
                                             ParagraphDir::RTL, cfg, d);
        QVector<VisualRange> rMark =
            shaper.visualRangesForLogicalRange(mark, 1, 2);
        int markGlyphs = 0;
        for (const VisualRange &r : rMark) markGlyphs += r.glyphCount;
        check(markGlyphs == mark.shapedGlyphs.size(),
              "selection-mark-keeps-base-cluster", QString::number(markGlyphs));

        const QString digitsText = QStringLiteral("المعرض 2026 (جناح A-12)");
        ShapedParagraph digits = shaper.shape(digitsText, {},
                                              ParagraphDir::RTL, cfg, d);
        QVector<VisualRange> rNum =
            shaper.visualRangesForLogicalRange(digits, 7, 11); // "2026"
        bool digitsInVisualOrder = !rNum.isEmpty();
        int selectedGlyphs = 0;
        QVector<int> selectedSources;
        for (const VisualRange &r : rNum) {
            const ShapedLine &line = digits.lines[r.lineNumber];
            for (int k = 0; k < r.glyphCount; ++k) {
                const ShapedGlyph &g = line.glyphs[r.firstGlyph + k];
                selectedSources.push_back(g.sourceIndex);
                ++selectedGlyphs;
            }
        }
        check(selectedGlyphs == 4, "selection-highlights-four-digit-glyphs",
              QString::number(selectedGlyphs));
        for (int i = 1; i < selectedSources.size(); ++i)
            if (selectedSources[i] <= selectedSources[i - 1])
                digitsInVisualOrder = false;
        check(digitsInVisualOrder, "digits-highlighted-in-visual-ltr-order",
              [&] {
                  QStringList s;
                  for (int v : selectedSources) s << QString::number(v);
                  return s.join(",");
              }());
        // The inverse hit test must return the same logical character.
        int hit = shaper.logicalIndexAtVisualGlyph(
            digits, rNum.first().lineNumber, rNum.first().firstGlyph);
        check(hit == 7, "hit-test-returns-cluster-source",
              QString::number(hit));
    }

    QFile f(outDir + "/layout-data.json");
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(QJsonDocument(allLayouts).toJson(QJsonDocument::Indented));

    QTextStream(stdout) << "\n" << g_checks - g_failures << "/" << g_checks
                        << " checks passed\n";
    return g_failures ? 1 : 0;
}
