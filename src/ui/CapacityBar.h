#pragma once

#include <QWidget>
#include "../core/IPodDevice.h"

class CapacityBar : public QWidget {
    Q_OBJECT

public:
    explicit CapacityBar(QWidget* parent = nullptr);

    void setBreakdown(const StorageBreakdown& breakdown);
    StorageBreakdown breakdown() const { return m_breakdown; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    struct SegmentRect {
        QRectF rect;
        FileCategory category;
        QString label;
        uint64_t bytes;
        double percent;
    };

    StorageBreakdown m_breakdown;
    QList<SegmentRect> m_segments;
};
