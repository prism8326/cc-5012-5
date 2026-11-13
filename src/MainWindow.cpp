#include "MainWindow.h"
#include "PreviewWidget.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimeEdit>
#include <QDir>
#include <QStandardPaths>
#include <QStatusBar>
#include <numeric>

namespace sub {

static const char *kDefaultFontsEnv = "SUBTITLER_FONT_DIR";

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_fm = std::make_unique<FontManager>();
    loadDefaultFonts();
    buildUi();

    // A representative mixed-script cue so the application opens with
    // meaningful Arabic/Hebrew + Latin digit + bracket content.
    SubtitleCue demo;
    demo.id = "cue-1";
    demo.startMs = 0;
    demo.endMs = 4000;
    demo.text = QStringLiteral(
        "مرحبا بكم في معرض 2026 (جناح A-12) — EXPO 2030! "
        "ברוכים הבאים לתערוכה (ביתן 7) Brand™");
    demo.forcedDir = ParagraphDir::Auto;
    m_project.cues.push_back(demo);
    m_project.layout.fontPreference = {
        "Noto Sans Arabic", "Noto Sans Hebrew", "DejaVu Sans"};
    refreshCueList();
    m_cueList->setCurrentRow(0);
}

void MainWindow::loadDefaultFonts()
{
    QStringList dirs;
    if (qEnvironmentVariableIsSet(kDefaultFontsEnv))
        dirs << qgetenv(kDefaultFontsEnv);
    dirs << "/workspace/.deps/local/usr/share/fonts/truetype/noto"
         << "/usr/share/fonts/truetype/dejavu";
    int total = 0;
    for (const QString &d : dirs) total += m_fm->loadDirectory(d);
    if (m_fontLabel)
        m_fontLabel->setText(tr("Loaded faces: %1").arg(total));
    m_project.fontDirectories = dirs;
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Multilingual Subtitle Shaping Workbench"));
    resize(1400, 900);

    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);

    // ---- left: cue list + editor ----------------------------------------
    auto *left = new QVBoxLayout;
    m_cueList = new QListWidget;
    m_cueList->setMaximumWidth(260);
    connect(m_cueList, &QListWidget::currentRowChanged, this,
            &MainWindow::selectCue);
    left->addWidget(new QLabel(tr("Cues (logical text)")));
    left->addWidget(m_cueList, 2);

    auto *cueBtns = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Add"));
    auto *delBtn = new QPushButton(tr("Remove"));
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addCue);
    connect(delBtn, &QPushButton::clicked, this, &MainWindow::removeCue);
    cueBtns->addWidget(addBtn);
    cueBtns->addWidget(delBtn);
    left->addLayout(cueBtns);

    m_editor = new QPlainTextEdit;
    m_editor->setPlaceholderText(tr("Edit logical text — stored verbatim"));
    connect(m_editor, &QPlainTextEdit::textChanged, this,
            [this] {
                if (m_currentCue >= 0 &&
                    m_currentCue < m_project.cues.size()) {
                    m_project.cues[m_currentCue].text = m_editor->toPlainText();
                    reshapeCurrent();
                }
            });
    left->addWidget(new QLabel(tr("Logical text editor")));
    left->addWidget(m_editor, 3);

    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(new QLabel(tr("Paragraph direction:")));
    m_dirCombo = new QComboBox;
    m_dirCombo->addItem(tr("Auto (first strong char)"), int(ParagraphDir::Auto));
    m_dirCombo->addItem(tr("Force LTR"), int(ParagraphDir::LTR));
    m_dirCombo->addItem(tr("Force RTL"), int(ParagraphDir::RTL));
    connect(m_dirCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_currentCue < 0) return;
        m_project.cues[m_currentCue].forcedDir =
            ParagraphDir(m_dirCombo->currentData().toInt());
        reshapeCurrent();
    });
    dirRow->addWidget(m_dirCombo);
    left->addLayout(dirRow);

    root->addLayout(left, 3);

    // ---- center: preview --------------------------------------------------
    auto *center = new QVBoxLayout;
    m_preview = new PreviewWidget(m_fm.get());
    center->addWidget(m_preview, 5);

    m_diagLabel = new QLabel;
    m_diagLabel->setWordWrap(true);
    m_diagLabel->setStyleSheet("color: #ffb46e;");
    center->addWidget(m_diagLabel);

    m_controlModel = new ControlCharModel(this);
    m_controlTable = new QTableView;
    m_controlTable->setModel(m_controlModel);
    m_controlTable->setMaximumHeight(150);
    center->addWidget(new QLabel(tr("Control / bidi characters (visible check)")));
    center->addWidget(m_controlTable, 1);
    root->addLayout(center, 5);

    // ---- right: config + actions -----------------------------------------
    auto *right = new QVBoxLayout;
    auto *boxGroup = new QGroupBox(tr("Layout"));
    auto *form = new QFormLayout(boxGroup);
    m_sizeSpin = new QDoubleSpinBox;
    m_sizeSpin->setRange(8, 240);
    m_sizeSpin->setValue(42);
    m_widthSpin = new QDoubleSpinBox;
    m_widthSpin->setRange(0, 7680);
    m_widthSpin->setValue(1680);
    m_marginSpin = new QDoubleSpinBox;
    m_marginSpin->setRange(0, 1000);
    m_marginSpin->setValue(120);
    m_anchorCombo = new QComboBox;
    m_anchorCombo->addItem(tr("Bottom center"));
    m_anchorCombo->addItem(tr("Bottom left"));
    m_anchorCombo->addItem(tr("Bottom right"));
    m_anchorCombo->addItem(tr("Top center"));
    form->addRow(tr("Font size px"), m_sizeSpin);
    form->addRow(tr("Max box width (0=auto)"), m_widthSpin);
    form->addRow(tr("Side margin px"), m_marginSpin);
    form->addRow(tr("Anchor"), m_anchorCombo);
    for (auto *w : {m_sizeSpin, m_widthSpin, m_marginSpin})
        connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &MainWindow::reshapeCurrent);
    connect(m_anchorCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::reshapeCurrent);
    right->addWidget(boxGroup);

    auto *dirGroup = new QGroupBox(tr("Direction boundary policy"));
    auto *dform = new QVBoxLayout(dirGroup);
    m_mirrorBrackets = new QCheckBox(tr("Mirror brackets (UAX #9 pair handling)"));
    m_numLtr = new QCheckBox(tr("Latin digits keep LTR order inside RTL"));
    m_currency = new QCheckBox(tr("Currency/percent binds to its number"));
    m_neutralBase = new QCheckBox(tr("Neutral punctuation resolves to base"));
    m_emphasisIso = new QCheckBox(tr("Inline emphasis opens level isolation"));
    m_mirrorBrackets->setChecked(true);
    m_numLtr->setChecked(true);
    m_currency->setChecked(true);
    m_emphasisIso->setChecked(true);
    for (auto *c : {m_mirrorBrackets, m_numLtr, m_currency, m_neutralBase,
                    m_emphasisIso}) {
        dform->addWidget(c);
        connect(c, &QCheckBox::toggled, this, &MainWindow::reshapeCurrent);
    }
    right->addWidget(dirGroup);

    m_fontLabel = new QLabel;
    right->addWidget(m_fontLabel);
    auto *fontBtn = new QPushButton(tr("Add font directory…"));
    connect(fontBtn, &QPushButton::clicked, this,
            &MainWindow::addFontDirectory);
    right->addWidget(fontBtn);

    auto *ackBtn = new QPushButton(tr("Acknowledge missing-glyph errors"));
    connect(ackBtn, &QPushButton::clicked, this,
            &MainWindow::acknowledgeMissing);
    right->addWidget(ackBtn);

    auto *openBtn = new QPushButton(tr("Open project…"));
    auto *saveBtn = new QPushButton(tr("Save project…"));
    auto *exportBtn = new QPushButton(tr("Export video + layout…"));
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openProject);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveProject);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportVideo);
    right->addWidget(openBtn);
    right->addWidget(saveBtn);
    right->addWidget(exportBtn);
    right->addStretch();
    root->addLayout(right, 2);

    setCentralWidget(central);
}

LayoutConfig MainWindow::collectConfig() const
{
    LayoutConfig cfg = m_project.layout;
    cfg.pointSize = m_sizeSpin->value();
    cfg.maxBoxWidth = m_widthSpin->value();
    cfg.marginLeft = cfg.marginRight = m_marginSpin->value();
    cfg.anchor = LayoutConfig::Anchor(m_anchorCombo->currentIndex());
    cfg.direction.mirrorBrackets = m_mirrorBrackets->isChecked();
    cfg.direction.europeanNumbersLtr = m_numLtr->isChecked();
    cfg.direction.currencyWithNumber = m_currency->isChecked();
    cfg.direction.neutralToBase = m_neutralBase->isChecked();
    cfg.direction.emphasisIsolation = m_emphasisIso->isChecked();
    if (cfg.fontPreference.isEmpty())
        cfg.fontPreference = {"Noto Sans Arabic", "Noto Sans Hebrew",
                              "DejaVu Sans"};
    return cfg;
}

void MainWindow::applyConfig(const LayoutConfig &cfg)
{
    m_sizeSpin->setValue(cfg.pointSize);
    m_widthSpin->setValue(cfg.maxBoxWidth);
    m_marginSpin->setValue(cfg.marginLeft);
    m_anchorCombo->setCurrentIndex(int(cfg.anchor));
    m_mirrorBrackets->setChecked(cfg.direction.mirrorBrackets);
    m_numLtr->setChecked(cfg.direction.europeanNumbersLtr);
    m_currency->setChecked(cfg.direction.currencyWithNumber);
    m_neutralBase->setChecked(cfg.direction.neutralToBase);
    m_emphasisIso->setChecked(cfg.direction.emphasisIsolation);
}

void MainWindow::refreshCueList()
{
    m_cueList->blockSignals(true);
    m_cueList->clear();
    for (const SubtitleCue &c : m_project.cues)
        m_cueList->addItem(QStringLiteral("%1  %2")
            .arg(c.id, c.text.left(28).replace('\n', ' ')));
    m_cueList->blockSignals(false);
}

void MainWindow::selectCue(int row)
{
    if (row < 0 || row >= m_project.cues.size()) return;
    m_currentCue = row;
    const SubtitleCue &c = m_project.cues[row];
    m_editor->blockSignals(true);
    m_editor->setPlainText(c.text);
    m_editor->blockSignals(false);
    m_dirCombo->setCurrentIndex(int(c.forcedDir));
    reshapeCurrent();
}

void MainWindow::syncEditorToCue()
{
    if (m_currentCue < 0) return;
    m_project.cues[m_currentCue].text = m_editor->toPlainText();
    m_project.cues[m_currentCue].forcedDir =
        ParagraphDir(m_dirCombo->currentData().toInt());
}

void MainWindow::reshapeCurrent()
{
    if (m_currentCue < 0 || m_currentCue >= m_project.cues.size()) return;
    syncEditorToCue();
    m_project.layout = collectConfig();
    const SubtitleCue &c = m_project.cues[m_currentCue];
    TextShaper shaper(m_fm.get());
    m_lastDiag = ShapeDiagnostics();
    m_lastPara = shaper.shape(c.text, c.styleRuns, c.forcedDir,
                              m_project.layout, m_lastDiag);
    m_preview->setContent(&m_lastPara, &m_project.layout, &m_lastDiag);
    m_controlModel->setHits(m_lastDiag.controls);

    QStringList notes;
    if (!m_lastDiag.missing.isEmpty()) {
        notes << tr("%1 missing-glyph error(s):")
                 .arg(m_lastDiag.missing.size());
        for (const auto &m : m_lastDiag.missing)
            notes << QStringLiteral("  [%1] %2")
                .arg(m.logicalIndex)
                .arg(m.reason);
    }
    notes << tr("%1 control/bidi characters listed below")
             .arg(m_lastDiag.controls.size());
    notes << tr("base level %1, %2 visual line(s), %3 glyph(s)")
             .arg(m_lastPara.baseLevel)
             .arg(m_lastPara.lines.size())
             .arg(std::accumulate(m_lastPara.lines.begin(),
                                  m_lastPara.lines.end(), 0,
                                  [](int n, const ShapedLine &l) {
                                      return n + l.glyphs.size();
                                  }));
    m_diagLabel->setText(notes.join("\n"));
    m_acknowledgedMissing = false;
}

void MainWindow::addCue()
{
    SubtitleCue c;
    c.id = QStringLiteral("cue-%1").arg(m_project.cues.size() + 1);
    c.startMs = m_project.cues.size() * 4000.0;
    c.endMs = c.startMs + 4000;
    m_project.cues.push_back(c);
    refreshCueList();
    m_cueList->setCurrentRow(m_project.cues.size() - 1);
}

void MainWindow::removeCue()
{
    if (m_currentCue < 0) return;
    m_project.cues.removeAt(m_currentCue);
    refreshCueList();
    m_currentCue = -1;
    m_cueList->setCurrentRow(qMax(0, m_project.cues.size() - 1));
}

void MainWindow::addFontDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select font directory"));
    if (dir.isEmpty()) return;
    int n = m_fm->loadDirectory(dir);
    m_fontLabel->setText(tr("Loaded faces (added %1): %2")
                             .arg(n).arg(m_fm->fonts().size()));
    m_project.fontDirectories << dir;
    reshapeCurrent();
}

void MainWindow::acknowledgeMissing()
{
    if (m_lastDiag.missing.isEmpty()) {
        QMessageBox::information(this, tr("No errors"),
                                 tr("Current cue has full glyph coverage."));
        return;
    }
    auto ret = QMessageBox::warning(
        this, tr("Export will still be blocked"),
        tr("Missing glyphs cannot be exported as boxes. Add a covering font "
           "or remove the character. Acknowledging only records the issue in "
           "the layout diagnostics."),
        QMessageBox::Ok);
    if (ret == QMessageBox::Ok) m_acknowledgedMissing = true;
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), {}, tr("Subtitle project (*.json)"));
    if (path.isEmpty()) return;
    Project loaded;
    QString err;
    if (!ProjectStore::load(loaded, path, &err)) {
        QMessageBox::critical(this, tr("Open failed"), err);
        return;
    }
    m_project = std::move(loaded);
    for (const QString &d : m_project.fontDirectories) m_fm->loadDirectory(d);
    for (const QString &f : m_project.fontFiles) m_fm->loadFile(f);
    applyConfig(m_project.layout);
    refreshCueList();
    if (!m_project.cues.isEmpty()) m_cueList->setCurrentRow(0);
}

void MainWindow::saveProject()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save project"), {}, tr("Subtitle project (*.json)"));
    if (path.isEmpty()) return;
    syncEditorToCue();
    QString err;
    if (!ProjectStore::save(m_project, path, &err))
        QMessageBox::critical(this, tr("Save failed"), err);
    else
        statusBar()->showMessage(tr("Saved %1 — logical text unchanged on reopen")
                                     .arg(path), 4000);
}

void MainWindow::exportVideo()
{
    if (!m_lastDiag.missing.isEmpty()) {
        QMessageBox::critical(
            this, tr("Export blocked"),
            tr("Resolve %1 missing-glyph error(s) first; boxes are never "
               "exported silently.").arg(m_lastDiag.missing.size()));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export video"), {}, tr("MP4 video (*.mp4)"));
    if (path.isEmpty()) return;
    syncEditorToCue();

    VideoExporter::Options opt;
    opt.ffmpegPath = qEnvironmentVariable("FFMPEG",
        "/workspace/.deps/bin/ffmpeg-7.0.2-arm64-static/ffmpeg");
    opt.outputPath = path;
    opt.layoutDataPath = QFileInfo(path).absolutePath() + "/" +
        QFileInfo(path).baseName() + "_layout.json";
    opt.fps = 25;

    VideoExporter exporter(m_fm.get());
    connect(&exporter, &VideoExporter::progress, this,
            [this](int pct, const QString &stage) {
                statusBar()->showMessage(tr("%1% — %2").arg(pct).arg(stage));
                QApplication::processEvents();
            });
    QString err;
    if (!exporter.exportProject(m_project, opt, err))
        QMessageBox::critical(this, tr("Export failed"), err);
    else
        QMessageBox::information(
            this, tr("Export complete"),
            tr("Video:\n%1\n\nLayout data:\n%2")
                .arg(path, opt.layoutDataPath));
}

} // namespace sub
