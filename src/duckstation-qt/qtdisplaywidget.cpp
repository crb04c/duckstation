#include "qtdisplaywidget.h"
#include "common/bitutils.h"
#include "qthostinterface.h"
#include "qtutils.h"
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtGui/QWindowStateChangeEvent>
#include <cmath>

#if !defined(WIN32) && !defined(APPLE)
#include <qpa/qplatformnativeinterface.h>
#endif

QtDisplayWidget::QtDisplayWidget(QWidget* parent) : QWidget(parent)
{
  // We want a native window for both D3D and OpenGL.
  setAutoFillBackground(false);
  setAttribute(Qt::WA_NativeWindow, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_PaintOnScreen, true);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
}

QtDisplayWidget::~QtDisplayWidget() = default;

qreal QtDisplayWidget::devicePixelRatioFromScreen() const
{
  QScreen* screen_for_ratio;
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
  screen_for_ratio = windowHandle()->screen();
#else
  screen_for_ratio = screen();
#endif
  if (!screen_for_ratio)
    screen_for_ratio = QGuiApplication::primaryScreen();

  return screen_for_ratio ? screen_for_ratio->devicePixelRatio() : static_cast<qreal>(1);
}

int QtDisplayWidget::scaledWindowWidth() const
{
  return static_cast<int>(std::ceil(static_cast<qreal>(width()) * devicePixelRatioFromScreen()));
}

int QtDisplayWidget::scaledWindowHeight() const
{
  return static_cast<int>(std::ceil(static_cast<qreal>(height()) * devicePixelRatioFromScreen()));
}

std::optional<WindowInfo> QtDisplayWidget::getWindowInfo() const
{
  WindowInfo wi;

  // Windows and Apple are easy here since there's no display connection.
#if defined(WIN32)
  wi.type = WindowInfo::Type::Win32;
  wi.window_handle = reinterpret_cast<void*>(winId());
#elif defined(__APPLE__)
  wi.type = WindowInfo::Type::MacOS;
  wi.window_handle = reinterpret_cast<void*>(winId());
#else
  QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
  const QString platform_name = QGuiApplication::platformName();
  if (platform_name == QStringLiteral("xcb"))
  {
    wi.type = WindowInfo::Type::X11;
    wi.display_connection = pni->nativeResourceForWindow("display", windowHandle());
    wi.window_handle = reinterpret_cast<void*>(winId());
  }
  else if (platform_name == QStringLiteral("wayland"))
  {
    wi.type = WindowInfo::Type::Wayland;
    wi.display_connection = pni->nativeResourceForWindow("display", windowHandle());
    wi.window_handle = pni->nativeResourceForWindow("surface", windowHandle());
  }
  else
  {
    qCritical() << "Unknown PNI platform " << platform_name;
    return std::nullopt;
  }
#endif

  wi.surface_width = scaledWindowWidth();
  wi.surface_height = scaledWindowHeight();
  wi.surface_scale = devicePixelRatioFromScreen();
  wi.surface_format = WindowInfo::SurfaceFormat::RGB8;

  return wi;
}

void QtDisplayWidget::setRelativeMode(bool enabled)
{
  if (m_relative_mouse_enabled == enabled)
    return;

  if (enabled)
  {
    m_relative_mouse_start_position = QCursor::pos();

    const QPoint center_pos = mapToGlobal(QPoint(width() / 2, height() / 2));
    QCursor::setPos(center_pos);
    m_relative_mouse_last_position = center_pos;
    grabMouse();
  }
  else
  {
    QCursor::setPos(m_relative_mouse_start_position);
    releaseMouse();
  }

  m_relative_mouse_enabled = enabled;
}

QPaintEngine* QtDisplayWidget::paintEngine() const
{
  return nullptr;
}

bool QtDisplayWidget::event(QEvent* event)
{
  switch (event->type())
  {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    {
      const QKeyEvent* key_event = static_cast<QKeyEvent*>(event);
      if (!key_event->isAutoRepeat())
        emit windowKeyEvent(QtUtils::KeyEventToInt(key_event), event->type() == QEvent::KeyPress);

      return true;
    }

    case QEvent::MouseMove:
    {
      const QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);

      if (!m_relative_mouse_enabled)
      {
        const qreal dpr = devicePixelRatioFromScreen();
        const int scaled_x = static_cast<int>(static_cast<qreal>(mouse_event->x()) * dpr);
        const int scaled_y = static_cast<int>(static_cast<qreal>(mouse_event->y()) * dpr);

        windowMouseMoveEvent(scaled_x, scaled_y);
      }
      else
      {
        const QPoint center_pos = mapToGlobal(QPoint((width() + 1) / 2, (height() + 1) / 2));
        const QPoint mouse_pos = mapToGlobal(mouse_event->pos());

        const int dx = mouse_pos.x() - center_pos.x();
        const int dy = mouse_pos.y() - center_pos.y();
        m_relative_mouse_last_position.setX(m_relative_mouse_last_position.x() + dx);
        m_relative_mouse_last_position.setY(m_relative_mouse_last_position.y() + dy);
        windowMouseMoveEvent(m_relative_mouse_last_position.x(), m_relative_mouse_last_position.y());
        QCursor::setPos(center_pos);

#if 0
        qCritical() << "center" << center_pos.x() << "," << center_pos.y();
        qCritical() << "mouse" << mouse_pos.x() << "," << mouse_pos.y();
        qCritical() << "dxdy" << dx << "," << dy;
#endif
      }

      return true;
    }

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonRelease:
    {
      const u32 button_index = CountTrailingZeros(static_cast<u32>(static_cast<const QMouseEvent*>(event)->button()));
      emit windowMouseButtonEvent(static_cast<int>(button_index + 1u), event->type() != QEvent::MouseButtonRelease);
      return true;
    }

    case QEvent::Resize:
    {
      QWidget::event(event);

      emit windowResizedEvent(scaledWindowWidth(), scaledWindowHeight());
      return true;
    }

    case QEvent::Close:
    {
      emit windowClosedEvent();
      QWidget::event(event);
      return true;
    }

    case QEvent::WindowStateChange:
    {
      QWidget::event(event);

      if (static_cast<QWindowStateChangeEvent*>(event)->oldState() & Qt::WindowMinimized)
        emit windowRestoredEvent();

      return true;
    }

    default:
      return QWidget::event(event);
  }
}
