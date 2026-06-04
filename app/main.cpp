#include "common/version.h"
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QColorSpace>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <rhi/qrhi.h>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif
#include "core/cache/imagecacheprovider.h"
#include "core/io_pool.h"
#include "core/playback/playbackcontroller.h"
#include "core/library/librarybrowser.h"
#include "core/detail/detailmanager.h"
#include "core/settings/settingsstore.h"
#include "core/server/servermanager.h"

#ifdef Q_OS_WIN
static void setDarkTitleBar(QWindow *w) {
  if (!w)
    return;
  HWND h = reinterpret_cast<HWND>(w->winId());
  BOOL dark = TRUE;
  DwmSetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                        sizeof(dark));
}
#endif
#include "platform/rendering/mpv/mpvrenderitem.h"

static QThreadPool s_ioPool;

QThreadPool &ioPool() {
    return s_ioPool;
}

int main(int argc, char *argv[]) {

  QLoggingCategory::setFilterRules("qt.gui.imageio=false");

  QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
  fmt.setRedBufferSize(16);
  fmt.setGreenBufferSize(16);
  fmt.setBlueBufferSize(16);
  fmt.setAlphaBufferSize(0);
  fmt.setColorSpace(QColorSpace(QColorSpace::Primaries::Bt2020,
                                  QColorSpace::TransferFunction::St2084));
  QSurfaceFormat::setDefaultFormat(fmt);

  // Graphics API selection (0=auto, 1=D3D11, 2=Vulkan, 3=OpenGL).
  // Must be set before any QQuickWindow is created — restart required on change.
  {
    SettingsStore tmpSettings;
    int api = tmpSettings.graphicsApi();
    switch (api) {
    case 1: QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11); break;
    case 2: QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);     break;
    case 3: QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);     break;
    default:
#ifdef Q_OS_WIN
      QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#else
      QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
      break;
    }
    qDebug() << "Graphics API:" << QQuickWindow::graphicsApi();
  }
  QQuickStyle::setStyle("Fusion");

  QGuiApplication app(argc, argv);
  app.setApplicationName(MFPLAYER_APP_NAME);
  app.setWindowIcon(QIcon(":/mfplayer/resources/appicon.ico"));

  qmlRegisterType<MpvRenderItem>("mfplayer", 1, 0, "MpvRenderItem");

  // Configure I/O thread pool: 2-4 threads for filesystem work.
  // Separate from globalInstance() so disk-blocked threads don't starve CPU tasks.
  s_ioPool.setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2));

  QQmlApplicationEngine qmlEngine;

  auto *serverMgr = new ServerManager(&app);
  auto *playbackCtrl = new PlaybackController(serverMgr->emby(), serverMgr->cache(), serverMgr->settings(), &app);
  auto *libraryBrowser = new LibraryBrowser(serverMgr->emby(), serverMgr->cache(), &app);

  // Restore sort preferences from settings
  libraryBrowser->setProperty("sortBy", serverMgr->settings()->sortBy());
  libraryBrowser->setProperty("sortAscending", serverMgr->settings()->sortAscending());
  QObject::connect(libraryBrowser, &LibraryBrowser::sortByChanged, serverMgr->settings(), [serverMgr, libraryBrowser]() {
      serverMgr->settings()->setSortBy(libraryBrowser->sortBy());
  });
  QObject::connect(libraryBrowser, &LibraryBrowser::sortAscendingChanged, serverMgr->settings(), [serverMgr, libraryBrowser]() {
      serverMgr->settings()->setSortAscending(libraryBrowser->sortAscending());
  });
  auto *detailMgr = new DetailManager(serverMgr->emby(), serverMgr->cache(), &app);

  QObject::connect(serverMgr, &ServerManager::loggedOut, playbackCtrl, &PlaybackController::stopPlayback);
  QObject::connect(serverMgr, &ServerManager::loggedOut, libraryBrowser, &LibraryBrowser::clearAll);
  QObject::connect(serverMgr, &ServerManager::loggedOut, detailMgr, &DetailManager::clearAll);
  QObject::connect(serverMgr, &ServerManager::librariesReady, libraryBrowser, &LibraryBrowser::onLibrariesFetched);
  QObject::connect(playbackCtrl, &PlaybackController::resumeProgressUpdated, libraryBrowser, &LibraryBrowser::refreshResume);

  qmlEngine.rootContext()->setContextProperty("Playback", playbackCtrl);
  qmlEngine.rootContext()->setContextProperty("Library", libraryBrowser);
  qmlEngine.rootContext()->setContextProperty("Detail", detailMgr);
  qmlEngine.rootContext()->setContextProperty("Server", serverMgr);

  // Application directory for resolving relative paths (fonts etc.)
  // When launched via "Open with", the working directory is not the app dir.
  qmlEngine.rootContext()->setContextProperty("_appDir", app.applicationDirPath());

  qmlEngine.addImageProvider(
      "imgcache", new ImageCacheProvider(serverMgr->cache()));

  // Pass command-line file path for system "Open with" integration
  QStringList args = app.arguments();
  QString startupFile;
  if (args.size() > 1) {
    QUrl url(args.at(1));
    startupFile = url.isLocalFile() ? url.toLocalFile() : args.at(1);
  }
  qmlEngine.rootContext()->setContextProperty("_appVersion", MFPLAYER_APP_NAME " v1.0");
  qmlEngine.rootContext()->setContextProperty("_qtVersion", QT_VERSION_STR);
  qmlEngine.rootContext()->setContextProperty("_startupFile", startupFile);

  qmlEngine.loadFromModule("mfplayer", "Main");

  if (qmlEngine.rootObjects().isEmpty())
    return -1;

  QQuickWindow *rootWin = nullptr;
  for (auto *obj : qmlEngine.rootObjects()) {
    if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
      win->setPersistentGraphics(true);
      // Request HDR swapchain via Qt 6.11 QRhi — must be set before scene graph init.
      // Uses 10-bit Rec.2020 PQ (R10G10B10A2) to match mpv's target-trc=pq target-prim=bt.2020.
      win->setProperty("_qt_sg_hdr_format", "hdr10");
      playbackCtrl->setRootWindow(win);
      rootWin = win;
#ifdef Q_OS_WIN
      setDarkTitleBar(win);
#endif
    }
  }

  // Warmup: pre-allocate fullscreen swapchain to avoid first-toggle GPU stall.
  // Deferred to after app.exec() starts so the UI can render first.
  if (rootWin) {
    QTimer::singleShot(100, rootWin, [rootWin]() {
        rootWin->showFullScreen();
        QCoreApplication::processEvents();
        rootWin->showNormal();
    });

    // Detect actual swapchain HDR status after warmup creates it.
    // Qt silently falls back to SDR on non-HDR systems even with _qt_sg_hdr_format set.
    QTimer::singleShot(300, rootWin, [rootWin, &qmlEngine, playbackCtrl]() {
        bool hdr = false;
        if (auto *sc = rootWin->swapChain()) {
            hdr = sc->format() != QRhiSwapChain::SDR;
        }
        qmlEngine.rootContext()->setContextProperty("_hdrActive", hdr);
        qDebug() << "HDR swapchain active:" << hdr;

        // Tell mpv the display capability so it picks the right target colorspace.
        // HDR → PQ/BT.2020, SDR → sRGB/BT.709. Avoids washed-out video on SDR screens.
        playbackCtrl->mpv()->updateHdrDisplayActive(hdr);
    });
  }

  return app.exec();
}
