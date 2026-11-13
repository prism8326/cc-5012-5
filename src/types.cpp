#include "types.h"

#include <QJsonArray>

namespace sub {

static const char *dirName(ParagraphDir d)
{
    switch (d) {
    case ParagraphDir::LTR: return "ltr";
    case ParagraphDir::RTL: return "rtl";
    default: return "auto";
    }
}

static ParagraphDir dirFrom(const QString &s)
{
    if (s == "ltr") return ParagraphDir::LTR;
    if (s == "rtl") return ParagraphDir::RTL;
    return ParagraphDir::Auto;
}

static const char *emphName(Emphasis e)
{
    switch (e) {
    case Emphasis::Bold: return "bold";
    case Emphasis::Italic: return "italic";
    case Emphasis::BoldItalic: return "bold-italic";
    default: return "normal";
    }
}

static Emphasis emphFrom(const QString &s)
{
    if (s == "bold") return Emphasis::Bold;
    if (s == "italic") return Emphasis::Italic;
    if (s == "bold-italic") return Emphasis::BoldItalic;
    return Emphasis::Normal;
}

QJsonObject SubtitleCue::toJson() const
{
    QJsonObject o;
    o["id"] = id;
    o["startMs"] = startMs;
    o["endMs"] = endMs;
    // Logical text is serialized verbatim; nothing is reordered on save.
    o["logicalText"] = text;
    o["direction"] = dirName(forcedDir);
    QJsonArray runs;
    for (const StyleRun &r : styleRuns) {
        QJsonObject ro;
        ro["start"] = r.start;
        ro["length"] = r.length;
        ro["emphasis"] = emphName(r.emphasis);
        ro["fontFamilyHint"] = r.fontFamilyHint;
        runs.append(ro);
    }
    o["styleRuns"] = runs;
    return o;
}

SubtitleCue SubtitleCue::fromJson(const QJsonObject &o)
{
    SubtitleCue c;
    c.id = o.value("id").toString();
    c.startMs = o.value("startMs").toDouble();
    c.endMs = o.value("endMs").toDouble();
    c.text = o.value("logicalText").toString();
    c.forcedDir = dirFrom(o.value("direction").toString());
    for (const QJsonValue &v : o.value("styleRuns").toArray()) {
        QJsonObject ro = v.toObject();
        StyleRun r;
        r.start = ro.value("start").toInt();
        r.length = ro.value("length").toInt();
        r.emphasis = emphFrom(ro.value("emphasis").toString());
        r.fontFamilyHint = ro.value("fontFamilyHint").toString();
        c.styleRuns.push_back(r);
    }
    return c;
}

QJsonObject DirectionPolicy::toJson() const
{
    QJsonObject o;
    o["mirrorBrackets"] = mirrorBrackets;
    o["europeanNumbersLtr"] = europeanNumbersLtr;
    o["currencyWithNumber"] = currencyWithNumber;
    o["neutralToBase"] = neutralToBase;
    o["emphasisIsolation"] = emphasisIsolation;
    return o;
}

DirectionPolicy DirectionPolicy::fromJson(const QJsonObject &o)
{
    DirectionPolicy p;
    p.mirrorBrackets = o.value("mirrorBrackets").toBool(true);
    p.europeanNumbersLtr = o.value("europeanNumbersLtr").toBool(true);
    p.currencyWithNumber = o.value("currencyWithNumber").toBool(true);
    p.neutralToBase = o.value("neutralToBase").toBool(false);
    p.emphasisIsolation = o.value("emphasisIsolation").toBool(true);
    return p;
}

static const char *anchorName(LayoutConfig::Anchor a)
{
    using A = LayoutConfig::Anchor;
    switch (a) {
    case A::BottomLeft: return "bottom-left";
    case A::BottomRight: return "bottom-right";
    case A::TopCenter: return "top-center";
    default: return "bottom-center";
    }
}

static LayoutConfig::Anchor anchorFrom(const QString &s)
{
    using A = LayoutConfig::Anchor;
    if (s == "bottom-left") return A::BottomLeft;
    if (s == "bottom-right") return A::BottomRight;
    if (s == "top-center") return A::TopCenter;
    return A::BottomCenter;
}

QJsonObject LayoutConfig::toJson() const
{
    QJsonObject o;
    o["videoWidth"] = videoWidth;
    o["videoHeight"] = videoHeight;
    o["marginLeft"] = marginLeft;
    o["marginRight"] = marginRight;
    o["marginTop"] = marginTop;
    o["marginBottom"] = marginBottom;
    o["pointSize"] = pointSize;
    o["lineSpacing"] = lineSpacing;
    o["anchor"] = anchorName(anchor);
    o["maxBoxWidth"] = maxBoxWidth;
    o["direction"] = direction.toJson();
    QJsonArray fonts;
    for (const QString &f : fontPreference) fonts.append(f);
    o["fontPreference"] = fonts;
    return o;
}

LayoutConfig LayoutConfig::fromJson(const QJsonObject &o)
{
    LayoutConfig c;
    c.videoWidth = o.value("videoWidth").toDouble(1920);
    c.videoHeight = o.value("videoHeight").toDouble(1080);
    c.marginLeft = o.value("marginLeft").toDouble(120);
    c.marginRight = o.value("marginRight").toDouble(120);
    c.marginTop = o.value("marginTop").toDouble(40);
    c.marginBottom = o.value("marginBottom").toDouble(140);
    c.pointSize = o.value("pointSize").toDouble(42);
    c.lineSpacing = o.value("lineSpacing").toDouble(1.25);
    c.anchor = anchorFrom(o.value("anchor").toString());
    c.maxBoxWidth = o.value("maxBoxWidth").toDouble(0);
    c.direction = DirectionPolicy::fromJson(o.value("direction").toObject());
    c.fontPreference.clear();
    for (const QJsonValue &v : o.value("fontPreference").toArray())
        c.fontPreference.push_back(v.toString());
    return c;
}

} // namespace sub
