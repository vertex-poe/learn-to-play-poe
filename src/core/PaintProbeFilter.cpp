#include "core/PaintProbeFilter.h"
#include "core/PerfProbe.h"

#include <QEvent>

bool PaintProbeFilter::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj)
    if (event->type() == QEvent::Paint) {
        if (m_role == Default)
            PerfProbe::instance().onDefaultPagePainted();
        else
            PerfProbe::instance().onSwapPagePainted();
    }
    return false;
}
