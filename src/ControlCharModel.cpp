#include "ControlCharModel.h"

namespace sub {

ControlCharModel::ControlCharModel(QObject *parent) : QAbstractTableModel(parent) {}

void ControlCharModel::setHits(const QVector<ControlCharHit> &hits)
{
    beginResetModel();
    m_hits = hits;
    endResetModel();
}

int ControlCharModel::rowCount(const QModelIndex &) const { return m_hits.size(); }
int ControlCharModel::columnCount(const QModelIndex &) const { return ColumnCount; }

QVariant ControlCharModel::data(const QModelIndex &idx, int role) const
{
    if (role != Qt::DisplayRole || !idx.isValid()) return {};
    const ControlCharHit &h = m_hits[idx.row()];
    switch (idx.column()) {
    case Index: return h.logicalIndex;
    case Codepoint: return QString::asprintf("U+%04X", h.codepoint);
    case Description: return h.description;
    }
    return {};
}

QVariant ControlCharModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole || o != Qt::Horizontal) return {};
    switch (section) {
    case Index: return tr("Logical index");
    case Codepoint: return tr("Codepoint");
    case Description: return tr("Character");
    }
    return {};
}

} // namespace sub
