#include "registermodel.h"
#include "common/register_names.h"
#include <QApplication>
#include <QBrush>
#include <QDebug>
#include <QPalette>
#include <QStringList>
#include <limits>

namespace Kites
{
namespace
{
QString formatVectorValue(const std::array<uint8_t, 16> &bytes, Base base, int elementWidth)
{
    const int elementBytes = elementWidth / 8;
    const int elementCount = 128 / elementWidth;
    const uint64_t mask = elementWidth == 64
        ? UINT64_MAX : ((uint64_t{1} << elementWidth) - 1u);

    QStringList elements;
    elements.reserve(elementCount);
    for (int lane = 0; lane < elementCount; ++lane)
    {
        uint64_t raw = 0;
        for (int byte = 0; byte < elementBytes; ++byte)
        {
            raw |= static_cast<uint64_t>(bytes[static_cast<size_t>(lane * elementBytes + byte)])
                   << (8 * byte);
        }
        raw &= mask;

        switch (base)
        {
        case Base::Decimal:
        {
            const uint64_t signBit = uint64_t{1} << (elementWidth - 1);
            int64_t signedValue = 0;
            if ((raw & signBit) == 0)
            {
                signedValue = static_cast<int64_t>(raw);
            }
            else
            {
                // Convert two's-complement magnitude first. This avoids an
                // out-of-range uint64_t -> int64_t conversion for negative
                // lanes, including INT64_MIN.
                const uint64_t magnitude = ((~raw) & mask) + 1u;
                if (magnitude == (uint64_t{1} << 63))
                    signedValue = std::numeric_limits<int64_t>::min();
                else
                    signedValue = -static_cast<int64_t>(magnitude);
            }
            elements.push_back(QString::number(signedValue));
            break;
        }
        case Base::Hexadecimal:
            elements.push_back("0x" + QString("%1").arg(static_cast<qulonglong>(raw),
                                                        elementWidth / 4, 16, QChar('0')).toUpper());
            break;
        case Base::Binary:
        {
            QString bits;
            bits.reserve(elementWidth + elementWidth / 8);
            for (int bit = elementWidth - 1; bit >= 0; --bit)
            {
                bits.append((raw & (uint64_t{1} << bit)) ? QLatin1Char('1') : QLatin1Char('0'));
                if (bit > 0 && bit % 8 == 0)
                    bits.append(QLatin1Char(' '));
            }
            elements.push_back("0b" + bits);
            break;
        }
        }
    }
    return "[" + elements.join(", ") + "]";
}

QString formatMaskValue(const std::array<uint8_t, 16> &bytes, Base base, int elementWidth)
{
    // RVV predicate registers store one bit per element, with lane 0 in bit 0.
    // Display each predicate bit as a scalar in the currently selected base.
    const int laneCount = 128 / elementWidth;
    QStringList lanes;
    lanes.reserve(laneCount);

    for (int lane = 0; lane < laneCount; ++lane)
    {
        const size_t byteIndex = static_cast<size_t>(lane / 8);
        const int bitIndex = lane % 8;
        const bool active = (bytes[byteIndex] & (uint8_t{1} << bitIndex)) != 0;

        switch (base)
        {
        case Base::Decimal:
            lanes.push_back(active ? QStringLiteral("1") : QStringLiteral("0"));
            break;
        case Base::Hexadecimal:
            lanes.push_back(active ? QStringLiteral("0x1") : QStringLiteral("0x0"));
            break;
        case Base::Binary:
            lanes.push_back(active ? QStringLiteral("0b1") : QStringLiteral("0b0"));
            break;
        }
    }

    return "[" + lanes.join(", ") + "]";
}
} // namespace

RegisterModel::RegisterModel(QObject *parent, RegisterFile *regfile) : QAbstractTableModel(parent)
{
    m_currentRegisterFile = regfile;
    connect(m_currentRegisterFile, &RegisterFile::updateRegister, this,
            &RegisterModel::updateRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::updateFRegister, this,
            &RegisterModel::updateFRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::updateVectorRegister, this,
            &RegisterModel::updateVectorRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::registerResetSignal, this,
            &RegisterModel::registerResetSlot);
}

int RegisterModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 96; // 32 GPRs + 32 FPRs + 32 vector registers
}

int RegisterModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 3;
} // Register name, alias, value

void RegisterModel::setDisplayBase(Base base)
{
    beginResetModel();
    m_displayBase = base;
    endResetModel();
}

QVariant RegisterModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant{};

    if (orientation == Qt::Horizontal)
    {
        switch (section)
        {
        case 0:
            return QString("Register");
        case 1:
            return QString("Alias");
        case 2:
            return QString("Value");
        }
    }

    return QVariant{};
}

QVariant RegisterModel::data(const QModelIndex &index, int role) const
{
    if (!m_currentRegisterFile)
        return QVariant{};

    if (role == Qt::TextAlignmentRole)
    {
        return Qt::AlignCenter;
    }
    if (role == Qt::BackgroundRole)
    {
        if (static_cast<size_t>(index.row()) == m_highlightedRegisterIndex)
        {
            return QBrush(QColor("#b13232fd")); // Highlight color for the updated register
        }
    }
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
    {
        size_t row = static_cast<size_t>(index.row());
        switch (index.column())
        {
        case 0: // Register Name
            if (row < 32)
            {
                return QString("x%1").arg(row);
            }
            else if (row < 64)
            {
                return QString("f%1").arg(row - 32);
            }
            else
            {
                return QString("v%1").arg(row - 64);
            }
        case 1: // Register Alias
        {
            if (row < 32)
                return QString(register_names::kGprAliases[row]);
            else if (row < 64)
                return QString(register_names::kFprAliases[row - 32]);
            else
                return QString("");
        }
        case 2: // Register Value
        {
            if (row < 32)
            {
                uint64_t value = m_currentRegisterFile->ReadGpr(row);
                switch (m_displayBase)
                {
                case Base::Binary:
                    return QString("0b%1").arg(QString::number(value, 2));
                case Base::Decimal:
                    return QString::number(static_cast<int64_t>(value));
                case Base::Hexadecimal:
                    return QString("0x%1").arg(QString::number(value, 16).toUpper());
                }
            }
            else if (row < 64)
            {
                int fIndex = row - 32;

                // read raw bits of FP register (you must have this function)
                uint64_t raw = m_currentRegisterFile->ReadFpr(fIndex);

                switch (m_displayBase)
                {
                case Base::Binary:
                {
                    return QString("0b%1").arg(QString::number(raw, 2));
                }

                case Base::Decimal:
                {
                    uint32_t upper32 = static_cast<uint32_t>(raw >> 32);
                    if (upper32 == 0)
                    {
                        // Single-precision float (F-extension): value in lower 32 bits
                        uint32_t lower32 = static_cast<uint32_t>(raw & 0xFFFFFFFF);
                        float fval;
                        memcpy(&fval, &lower32, sizeof(float));
                        return QString::number(static_cast<double>(fval), 'g', 8);
                    }
                    else
                    {
                        // Double-precision (D-extension): full 64 bits
                        double dval;
                        memcpy(&dval, &raw, sizeof(double));
                        return QString::number(dval, 'g', 16);
                    }
                }

                case Base::Hexadecimal:
                {
                    return QString("0x%1").arg(QString::number(raw, 16).toUpper());
                }
                }
            }
            else if (row < 96)
            {
                // Interpret each 128-bit vector register as SEW-sized
                // little-endian lanes, using the selected display base.
                const auto bytes = m_currentRegisterFile->ReadVector(row - 64);
                const QString normalValue =
                    formatVectorValue(bytes, m_displayBase, m_vectorElementWidth);

                if (row == 64)
                {
                    // v0 is also the implicit RVV predicate register for masked
                    // operations. Show both interpretations: ordinary vector
                    // data and the per-lane predicate bits. This avoids hiding
                    // valid data when software uses v0 as a normal vector source.
                    return normalValue + "  |  mask: "
                           + formatMaskValue(bytes, m_displayBase, m_vectorElementWidth);
                }

                return normalValue;
            }
            else
            {
                return QString("");
            }
        }
        default:
            return QVariant{};
        }
    }
    return QVariant{};
}

void RegisterModel::setVectorElementWidth(int widthBits)
{
    if (widthBits != 8 && widthBits != 16 && widthBits != 32 && widthBits != 64)
        return;
    if (m_vectorElementWidth == widthBits)
        return;
    beginResetModel();
    m_vectorElementWidth = widthBits;
    endResetModel();
}

void RegisterModel::changeRegisterFile(RegisterFile *regfile)
{
    beginResetModel();
    m_currentRegisterFile = regfile;
    connect(m_currentRegisterFile, &RegisterFile::updateRegister, this,
            &RegisterModel::updateRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::updateFRegister, this,
            &RegisterModel::updateFRegisterValue);
    connect(m_currentRegisterFile, &RegisterFile::updateVectorRegister, this,
            &RegisterModel::updateVectorRegisterValue);
    // maybe we can directly connect it to
    connect(m_currentRegisterFile, &RegisterFile::registerResetSignal, this,
            &RegisterModel::registerResetSlot);
    endResetModel();
}

void RegisterModel::registerResetSlot()
{
    beginResetModel();
    endResetModel();
}

void RegisterModel::updateRegisterValue(size_t regIndex, uint64_t value)
{
    //QModelIndex index = this->index(static_cast<int>(regIndex), 2);
    m_highlightedRegisterIndex = regIndex; // Update highlighted register index
    // emit dataChanged(index,index);
    beginResetModel();
    endResetModel();
}

void RegisterModel::updateFRegisterValue(size_t regIndex, uint64_t value)
{
    //QModelIndex index = this->index(static_cast<int>(regIndex + 32), 2);
    m_highlightedRegisterIndex = regIndex + 32; // Update highlighted register index
    // emit dataChanged(index,index);
    beginResetModel();
    endResetModel();
}

void RegisterModel::updateVectorRegisterValue(size_t regIndex)
{
    m_highlightedRegisterIndex = regIndex + 64;
    beginResetModel();
    endResetModel();
}
} // namespace Kites
