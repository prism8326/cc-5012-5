#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <cstdio>

#include "ProjectStore.h"

using namespace sub;

static int failures = 0;
static void check(bool c, const QString &n)
{
    QTextStream(stdout) << (c ? "PASS " : "FAIL ") << n << "\n";
    if (!c) ++failures;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;

    Project p;
    p.name = "roundtrip";
    p.fontFiles = QStringList{
        "/workspace/.deps/local/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf"};
    p.fontDirectories = QStringList{
        "/workspace/.deps/local/usr/share/fonts/truetype/noto"};
    p.layout.pointSize = 55;
    p.layout.direction.mirrorBrackets = false;
    p.layout.direction.neutralToBase = true;
    p.layout.fontPreference = {"Noto Sans Arabic", "DejaVu Sans"};

    // Deliberately include bidi marks, combining marks, brackets and digits.
    SubtitleCue c;
    c.id = "c1";
    c.startMs = 100;
    c.endMs = 2500;
    c.text = QStringLiteral("مَعرض 2026 (جناح A-12) ‎שלום‎ Brand™");
    c.forcedDir = ParagraphDir::RTL;
    StyleRun r;
    r.start = c.text.indexOf("Brand");
    r.length = 5;
    r.emphasis = Emphasis::Bold;
    r.fontFamilyHint = "DejaVu Sans";
    c.styleRuns.push_back(r);
    p.cues.push_back(c);

    const QString path = dir.path() + "/project.json";
    QString err;
    check(ProjectStore::save(p, path, &err), "save");
    check(err.isEmpty(), "save-no-error");

    Project q;
    check(ProjectStore::load(q, path, &err), "load");
    check(err.isEmpty(), "load-no-error");
    check(q.cues.size() == 1, "cue-count");
    check(q.cues[0].text == c.text, "logical-text-byte-identical");
    check(q.cues[0].forcedDir == ParagraphDir::RTL, "direction-preserved");
    check(q.cues[0].styleRuns.size() == 1, "style-run-count");
    check(q.cues[0].styleRuns[0].emphasis == Emphasis::Bold, "emphasis-preserved");
    check(q.cues[0].styleRuns[0].fontFamilyHint == "DejaVu Sans",
          "family-hint-preserved");
    check(q.layout.pointSize == 55, "font-size-preserved");
    check(q.layout.direction.mirrorBrackets == false, "policy-flag-preserved");
    check(q.layout.direction.neutralToBase == true, "neutral-policy-preserved");
    check(q.layout.fontPreference == p.layout.fontPreference,
          "font-preference-preserved");

    // Explicit byte comparison of the logical text against the JSON payload.
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QByteArray raw = f.readAll();
    check(raw.contains(c.text.toUtf8()), "json-contains-verbatim-logical-text");

    // Second save of the loaded project must be stable.
    const QString path2 = dir.path() + "/project2.json";
    ProjectStore::save(q, path2, &err);
    QFile f2(path2);
    f2.open(QIODevice::ReadOnly);
    check(f2.readAll() == raw, "resave-is-stable");

    return failures ? 1 : 0;
}
