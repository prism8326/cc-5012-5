#ifndef SUBTITLER_MAINWINDOW_H
#define SUBTITLER_MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <memory>

#include "FontManager.h"
#include "TextShaper.h"
#include "ProjectStore.h"
#include "VideoExporter.h"
#include "ControlCharModel.h"

class QPlainTextEdit;
class QTableView;
class QListWidget;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;

namespace sub {
class PreviewWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void reshapeCurrent();
    void selectCue(int row);
    void addCue();
    void removeCue();
    void openProject();
    void saveProject();
    void exportVideo();
    void addFontDirectory();
    void acknowledgeMissing();

private:
    void buildUi();
    void loadDefaultFonts();
    void refreshCueList();
    void syncEditorToCue();
    LayoutConfig collectConfig() const;
    void applyConfig(const LayoutConfig &cfg);

    std::unique_ptr<FontManager> m_fm;
    Project m_project;
    int m_currentCue = -1;
    bool m_acknowledgedMissing = false;

    // Owned by Qt parent/child.
    QListWidget *m_cueList = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QComboBox *m_dirCombo = nullptr;
    PreviewWidget *m_preview = nullptr;
    QTableView *m_controlTable = nullptr;
    ControlCharModel *m_controlModel = nullptr;
    QLabel *m_diagLabel = nullptr;
    QLabel *m_fontLabel = nullptr;

    QDoubleSpinBox *m_sizeSpin = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QDoubleSpinBox *m_marginSpin = nullptr;
    QComboBox *m_anchorCombo = nullptr;
    QCheckBox *m_mirrorBrackets = nullptr;
    QCheckBox *m_numLtr = nullptr;
    QCheckBox *m_currency = nullptr;
    QCheckBox *m_neutralBase = nullptr;
    QCheckBox *m_emphasisIso = nullptr;

    ShapeDiagnostics m_lastDiag;
    ShapedParagraph m_lastPara;
};

} // namespace sub
#endif
