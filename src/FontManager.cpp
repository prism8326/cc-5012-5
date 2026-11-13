#include "FontManager.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QDebug>
#include <array>
#include <cstring>

namespace sub {

static const std::array<const char32_t, 2> kJoinerCp = {0x200D, 0x200C};

FontManager::FontManager()
{
    if (FT_Init_FreeType(&m_ft) != 0)
        qFatal("FreeType initialization failed");
}

FontManager::~FontManager()
{
    for (LoadedFont *f : m_fonts) {
        if (f->hbFont) hb_font_destroy(f->hbFont);
        if (f->ftFace) FT_Done_Face(f->ftFace);
        delete f;
    }
    if (m_ft) FT_Done_FreeType(m_ft);
}

static bool isFontFile(const QString &name)
{
    static const QSet<QString> exts = {"ttf", "otf", "ttc", "woff", "woff2"};
    return exts.contains(QFileInfo(name).suffix().toLower());
}

static QString sfntName(FT_Face face, FT_UShort nameId)
{
    const FT_UInt count = FT_Get_Sfnt_Name_Count(face);
    for (FT_UInt i = 0; i < count; ++i) {
        FT_SfntName sn;
        if (FT_Get_Sfnt_Name(face, i, &sn) != 0 || sn.name_id != nameId) continue;
        // platform 0/3 => UTF-16BE; platform 1 => Latin-1; platform 2 => SJIS-ish.
        if (sn.platform_id == 0 || sn.platform_id == 3) {
            QVector<char16_t> units(sn.string_len / 2);
            for (size_t k = 0; k < units.size(); ++k)
                units[int(k)] = char16_t((sn.string[k * 2] << 8) | sn.string[k * 2 + 1]);
            return QString::fromUtf16(units.constData(), units.size()).trimmed();
        }
        return QString::fromLatin1(
            reinterpret_cast<const char *>(sn.string), sn.string_len).trimmed();
    }
    return {};
}

int FontManager::loadDirectory(const QString &dir)
{
    QDir d(dir);
    if (!d.exists()) return 0;
    int added = 0;
    const QFileInfoList entries = d.entryInfoList(QDir::Files | QDir::Readable);
    for (const QFileInfo &fi : entries)
        if (isFontFile(fi.fileName())) added += loadFile(fi.absoluteFilePath());
    return added;
}

int FontManager::loadFile(const QString &file)
{
    QByteArray path = file.toUtf8();
    FT_Face face = nullptr;
    int added = 0;
    FT_Long count = 1;
    // Probe face count for collections.
    if (FT_New_Face(m_ft, path.constData(), -1, &face) == 0) {
        count = face->num_faces;
        FT_Done_Face(face);
        face = nullptr;
    }
    for (FT_Long i = 0; i < count; ++i) {
        if (FT_New_Face(m_ft, path.constData(), i, &face) != 0) continue;
        auto *lf = new LoadedFont;
        lf->file = file;
        lf->family = sfntName(face, 1);
        if (lf->family.isEmpty()) lf->family = face->family_name;
        lf->style = sfntName(face, 2);
        if (lf->style.isEmpty() && face->style_name) lf->style = face->style_name;
        lf->bold = face->style_flags & FT_STYLE_FLAG_BOLD;
        lf->italic = face->style_flags & FT_STYLE_FLAG_ITALIC;
        lf->unitsPerEm = face->units_per_EM > 0 ? face->units_per_EM : 1000.0;
        lf->ftFace = face;
        lf->hbFont = hb_ft_font_create_referenced(face);

        // Build the coverage table from cmap so fallback decisions do not
        // depend on shaping a glyph and guessing from .notdef.
        FT_UInt gindex = 0;
        for (FT_ULong c = FT_Get_First_Char(face, &gindex); gindex != 0;
             c = FT_Get_Next_Char(face, c, &gindex)) {
            lf->coverage.insert(char32_t(c));
        }

        lf->fontId = m_fonts.size();
        m_fonts.push_back(lf);
        m_byFamily[lf->family.toLower()].push_back(lf->fontId);
        ++added;
        face = nullptr;
    }
    if (face) FT_Done_Face(face);
    return added;
}

bool FontManager::covers(LoadedFont *f, char32_t cp) const
{
    if (!f) return false;
    if (f->coverage.contains(cp)) return true;
    // Joiners have no standalone glyph but must not be reported missing:
    // they influence shaping of the surrounding cluster.
    for (char32_t j : kJoinerCp) if (cp == j) return true;
    if (cp == 0xFEFF) return true; // BOM / zero-width no-break space
    return false;
}

static bool emphasisMatches(LoadedFont *f, Emphasis want)
{
    const bool wantBold = (want == Emphasis::Bold || want == Emphasis::BoldItalic);
    const bool wantItalic = (want == Emphasis::Italic || want == Emphasis::BoldItalic);
    if (wantBold && !f->bold) return false;
    if (wantItalic && !f->italic) return false;
    return true;
}

static bool emphasisExact(LoadedFont *f, Emphasis want)
{
    const bool wantBold = (want == Emphasis::Bold || want == Emphasis::BoldItalic);
    const bool wantItalic = (want == Emphasis::Italic || want == Emphasis::BoldItalic);
    return f->bold == wantBold && f->italic == wantItalic;
}

LoadedFont *FontManager::resolve(char32_t cp, Emphasis em,
                                 const QString &familyHint,
                                 const QStringList &preference) const
{
    // For a family's id list, prefer an exact style match, then any font that
    // satisfies the requested emphasis (e.g. regular when no italic exists),
    // then any face with coverage as a last resort. Regular text must never
    // silently pick a bold face just because it was loaded first.
    auto tryIds = [&](const QVector<int> &ids) -> LoadedFont * {
        for (int phase = 0; phase < 3; ++phase) {
            for (int id : ids) {
                LoadedFont *f = m_fonts[id];
                if (!covers(f, cp)) continue;
                if (phase == 0 && emphasisExact(f, em)) return f;
                if (phase == 1 && emphasisMatches(f, em)) return f;
                if (phase == 2) return f;
            }
        }
        return nullptr;
    };

    // 1. Explicit family hint (brand names).
    if (!familyHint.isEmpty()) {
        auto it = m_byFamily.constFind(familyHint.toLower());
        if (it != m_byFamily.constEnd())
            if (LoadedFont *f = tryIds(it.value())) return f;
    }
    // 2. Configured preference order, exact/acceptable style first.
    for (const QString &fam : preference) {
        auto it = m_byFamily.constFind(fam.toLower());
        if (it == m_byFamily.constEnd()) continue;
        if (LoadedFont *f = tryIds(it.value())) return f;
    }
    // 3. Any loaded font with coverage in the same style priority.
    QVector<int> all;
    all.reserve(m_fonts.size());
    for (int i = 0; i < m_fonts.size(); ++i) all.push_back(i);
    return tryIds(all);
}

double FontManager::scaleForPx(LoadedFont *f, double pixelSize) const
{
    return pixelSize / f->unitsPerEm;
}

LoadedFont *FontManager::byId(int id) const
{
    if (id < 0 || id >= m_fonts.size()) return nullptr;
    return m_fonts[id];
}

QString FontManager::describe(int id) const
{
    LoadedFont *f = byId(id);
    if (!f) return QStringLiteral("<invalid font id %1>").arg(id);
    return QStringLiteral("%1 %2 (%3)").arg(f->family, f->style, f->file);
}

} // namespace sub
