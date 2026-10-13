#include "CapacityBar.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QToolTip>
#include <QLinearGradient>

CapacityBar::CapacityBar(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(28);
}

void CapacityBar::setBreakdown(const StorageBreakdown& breakdown) {
    m_breakdown = breakdown;
    update();
}

QSize CapacityBar::sizeHint() const {
    return QSize(400, 32);
}

void CapacityBar::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    m_segments.clear();

    const qreal w = width();
    const qreal h = height();
    const qreal radius = 8.0;

    QRectF barRect(0, 0, w, h);

    // Fondo (Espacio Libre / Base)
    QPainterPath clipPath;
    clipPath.addRoundedRect(barRect, radius, radius);
    painter.setClipPath(clipPath);

    // Fondo base metálico suave estilo iTunes 10
    painter.fillRect(barRect, QColor(218, 222, 228));

    if (m_breakdown.totalBytes == 0) {
        painter.setPen(QColor(110, 118, 128));
        painter.drawText(barRect, Qt::AlignCenter, "Sin almacenamiento detectado");
        return;
    }

    struct CatDef {
        FileCategory cat;
        QString name;
        uint64_t bytes;
        QColor colorStart;
        QColor colorEnd;
    };

    // Paleta oficial icónica de iTunes 10:
    // Audio: Ámbar / Naranja brillante
    // Video: Cian / Azul turquesa
    // Podcasts: Púrpura / Violeta
    // Fotos: Oro / Amarillo cálido
    // Otros: Bronce / Gris pizarra
    QList<CatDef> defs = {
        {FileCategory::Audio, "Música", m_breakdown.audioBytes, QColor(255, 149, 0), QColor(224, 76, 0)},
        {FileCategory::Video, "Video", m_breakdown.videoBytes, QColor(0, 190, 214), QColor(0, 126, 148)},
        {FileCategory::Podcasts, "Podcasts", m_breakdown.podcastBytes, QColor(168, 62, 196), QColor(114, 24, 142)},
        {FileCategory::Photos, "Fotos", m_breakdown.photoBytes, QColor(255, 198, 26), QColor(230, 150, 0)},
        {FileCategory::Other, "Otros", m_breakdown.otherBytes, QColor(126, 142, 154), QColor(82, 98, 110)}
    };

    qreal currentX = 0.0;
    for (const auto& item : defs) {
        if (item.bytes == 0) continue;

        double fraction = static_cast<double>(item.bytes) / static_cast<double>(m_breakdown.totalBytes);
        qreal segWidth = fraction * w;
        if (segWidth < 1.0 && fraction > 0.0) segWidth = 1.0;

        QRectF segRect(currentX, 0, segWidth, h);

        QLinearGradient grad(segRect.topLeft(), segRect.bottomLeft());
        grad.setColorAt(0.0, item.colorStart);
        grad.setColorAt(1.0, item.colorEnd);
        painter.fillRect(segRect, grad);

        // Separador sutil entre segmentos
        painter.setPen(QColor(0, 0, 0, 45));
        painter.drawLine(QPointF(currentX + segWidth, 0), QPointF(currentX + segWidth, h));

        // Almacenar el segmento para detección en mouseMoveEvent
        SegmentRect sr;
        sr.rect = segRect;
        sr.category = item.cat;
        sr.label = item.name;
        sr.bytes = item.bytes;
        sr.percent = fraction * 100.0;
        m_segments.append(sr);

        currentX += segWidth;
    }

    // El remanente es Espacio Libre (Gris metálico plateado de iTunes)
    if (m_breakdown.freeBytes > 0 && currentX < w) {
        QRectF freeRect(currentX, 0, w - currentX, h);
        QLinearGradient freeGrad(freeRect.topLeft(), freeRect.bottomLeft());
        freeGrad.setColorAt(0.0, QColor(226, 230, 236));
        freeGrad.setColorAt(1.0, QColor(194, 200, 208));
        painter.fillRect(freeRect, freeGrad);

        SegmentRect sr;
        sr.rect = freeRect;
        sr.category = FileCategory::Free;
        sr.label = "Libre";
        sr.bytes = m_breakdown.freeBytes;
        sr.percent = (static_cast<double>(m_breakdown.freeBytes) / m_breakdown.totalBytes) * 100.0;
        m_segments.append(sr);
    }

    // Brillo de cristal / reflejo glossy característico de iTunes 10
    QRectF glossRect(0, 0, w, h * 0.45);
    QLinearGradient glossGrad(0, 0, 0, h * 0.45);
    glossGrad.setColorAt(0.0, QColor(255, 255, 255, 130));
    glossGrad.setColorAt(1.0, QColor(255, 255, 255, 25));
    painter.fillRect(glossRect, glossGrad);

    // Borde exterior metálico con bisel
    painter.setClipping(false);
    painter.setPen(QPen(QColor(128, 134, 144), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(barRect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

void CapacityBar::mouseMoveEvent(QMouseEvent* event) {
    QPointF pos = event->position();
    for (const auto& seg : m_segments) {
        if (seg.rect.contains(pos)) {
            QString tip = QString("%1: %2 (%3%)")
                .arg(seg.label)
                .arg(IPodDevice::formatBytes(seg.bytes))
                .arg(QString::number(seg.percent, 'f', 1));
            QToolTip::showText(event->globalPosition().toPoint(), tip, this);
            return;
        }
    }
    QToolTip::hideText();
}
