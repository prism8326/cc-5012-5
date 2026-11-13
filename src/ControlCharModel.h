#ifndef SUBTITLER_CONTROLCHARMODEL_H
#define SUBTITLER_CONTROLCHARMODEL_H

#include <QAbstractTableModel>
#include "types.h"

namespace sub {

// Visible inspection of control/bidi characters in the input. Nothing is
// stripped silently: every hit shows its logical index, codepoint and role.
class ControlCharModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Index = 0, Codepoint, Description, ColumnCount };

    explicit ControlCharModel(QObject *parent = nullptr);
    void setHits(const QVector<ControlCharHit> &hits);
    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation, int role) const override;

private:
    QVector<ControlCharHit> m_hits;
};

} // namespace sub
#endif
