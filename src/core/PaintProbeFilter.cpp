#include "core/PaintProbeFilter.h"
#include "core/PerfProbe.h"

#include <QEvent>

bool PaintProbeFilter::eventFilter(QObject * /*obj*/, QEvent *event)
{
    if (event->type() == QEvent::Paint) {
        if (m_role == Default)
            PerfProbe::instance().onDefaultPagePainted();
        else
            PerfProbe::instance().onSwapPagePainted();
    }
    return false; // never consume — let the widget handle its own paint
}
