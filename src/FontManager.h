#ifndef SUBTITLER_FONTMANAGER_H
#define SUBTITLER_FONTMANAGER_H

#include <QString>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include "types.h"

namespace sub {

struct LoadedFont {
    QString file;
    QString family;
    QString style;                 // e.g. "Regular", "Bold"
    bool bold = false;
    bool italic = false;
    FT_Face ftFace = nullptr;
    hb_font_t *hbFont = nullptr;
    double unitsPerEm = 1;
    // Coverage set built from the cmap; enough for deterministic fallback.
    QSet<char32_t> coverage;
    int fontId = -1;
};

class FontManager {
public:
    FontManager();
    ~FontManager();
    FontManager(const FontManager &) = delete;
    FontManager &operator=(const FontManager &) = delete;

    // All loads are explicit: no silent platform substitution.
    // Returns number of faces added.
    int loadDirectory(const QString &dir);
    int loadFile(const QString &file);

    // Same physical font set is used by the preview widget and the export
    // renderer; the FontManager instance is shared.
    const QVector<LoadedFont *> &fonts() const { return m_fonts; }

    // Decide which loaded font renders cp, honoring a family hint and an
    // emphasis. Returns nullptr when nothing covers the codepoint; the caller
    // records a missing-glyph diagnostic instead of painting a box quietly.
    LoadedFont *resolve(char32_t cp, Emphasis em, const QString &familyHint,
                        const QStringList &preference) const;

    bool covers(LoadedFont *f, char32_t cp) const;

    // Convert HarfBuzz 26.6 fixed-point coordinates to pixels for a size.
    double scaleForPx(LoadedFont *f, double pixelSize) const;

    LoadedFont *byId(int id) const;

    QString describe(int id) const;

private:
    FT_Library m_ft = nullptr;
    QVector<LoadedFont *> m_fonts;
    QHash<QString, QVector<int>> m_byFamily; // family -> font ids
};

} // namespace sub
#endif
