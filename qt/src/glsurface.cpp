#include "glsurface.h"
#include "i18n.h"
#include "player.h"
#include "mpvlifetime.h"
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLTextureBlitter>
#include <QTimer>
#include <mpv/render_gl.h>

static void *resolveGl(void *, const char *name) {
  auto *ctx = QOpenGLContext::currentContext();
  return ctx ? reinterpret_cast<void *>(ctx->getProcAddress(name)) : nullptr;
}
GlSurface::GlSurface(Player *p) : QOpenGLWidget(p), owner(p) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setUpdateBehavior(QOpenGLWidget::PartialUpdate);
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setSamples(0);
  fmt.setSwapInterval(owner->requestedSwapInterval());
  setFormat(fmt);
  setTextureFormat(GL_RGBA8);
}
GlSurface::~GlSurface() { release(); }
void GlSurface::initializeGL() {
  owner->setProperty("foxicmpSwapIntervalReported",
                     context()->format().swapInterval());
  blitter = std::make_unique<QOpenGLTextureBlitter>();
  if (!blitter->create()) {
    QTimer::singleShot(0, owner, [p = owner] {
      p->fallbackToSoftware(uiTr("Недоступен OpenGL blitter"));
    });
    return;
  }
  renderContext = std::make_unique<QOpenGLContext>();
  renderContext->setFormat(context()->format());
  renderContext->setShareContext(context());
  auto fail = [this](const QString &reason) {
    QTimer::singleShot(0, owner,
                       [p = owner, reason] { p->fallbackToSoftware(reason); });
  };
  if (!renderContext->create() ||
      !QOpenGLContext::areSharing(context(), renderContext.get())) {
    fail(uiTr("Не удалось создать общий OpenGL-контекст"));
    return;
  }
  renderSurface = std::make_unique<QOffscreenSurface>();
  renderSurface->setFormat(renderContext->format());
  renderSurface->create();
  if (!renderSurface->isValid() ||
      !renderContext->makeCurrent(renderSurface.get())) {
    fail(uiTr("Недоступна поверхность OpenGL"));
    return;
  }
  auto *fn = renderContext->extraFunctions();
  const auto driver =
      QByteArray(reinterpret_cast<const char *>(fn->glGetString(GL_RENDERER)));
  owner->setProperty("foxicmpGlDriver", QString::fromLatin1(driver));
  if (driver.contains("llvmpipe") || driver.contains("softpipe")) {
    renderContext->doneCurrent();
    makeCurrent();
    fail(uiTr("Программный драйвер Mesa: используется проверенный программный "
              "вывод"));
    return;
  }
  fn->glGenFramebuffers(1, &framebuffer);
  mpv_opengl_init_params gl{resolveGl, nullptr};
  mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE,
                                const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                               {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
                               {MPV_RENDER_PARAM_INVALID, nullptr}};
  const int result =
      mpv_render_context_create(&owner->renderer, owner->handle, params);
  renderContext->doneCurrent();
  makeCurrent();
  if (result < 0) {
    fail(QString::fromUtf8(mpv_error_string(result)));
    return;
  }
  mpv_render_context_set_update_callback(owner->renderer,Player::redraw,owner->lifetime.get());
  connect(this, &QOpenGLWidget::frameSwapped, owner, [p = owner] {
    if (p->renderer)
      mpv_render_context_report_swap(p->renderer);
  });
  connect(
      context(), &QOpenGLContext::aboutToBeDestroyed, this,
      [this] { release(); }, Qt::DirectConnection);
  owner->rendererReady();
}
void GlSurface::paintGL() {
  QElapsedTimer timer;
  timer.start();
  if (!owner->renderer || !renderContext || !renderSurface)
    return;
  if (!renderContext->makeCurrent(renderSurface.get()))
    return;
  auto *fn = renderContext->extraFunctions();
  const QSize pixels = size() * devicePixelRatioF();
  if (textureSize != pixels) {
    if (!texture)
      fn->glGenTextures(1, &texture);
    fn->glBindTexture(GL_TEXTURE_2D, texture);
    fn->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    fn->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    fn->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.width(),
                     pixels.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    textureSize = pixels;
  }
  fn->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  fn->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, texture, 0);
  if (fn->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    renderContext->doneCurrent();
    makeCurrent();
    return;
  }
  const double scale = devicePixelRatioF();
  mpv_opengl_fbo target{int(framebuffer), qRound(width() * scale),
                        qRound(height() * scale), GL_RGBA8};
  int flip = 1, block = 0;
  mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &target},
                               {MPV_RENDER_PARAM_FLIP_Y, &flip},
                               {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                               {MPV_RENDER_PARAM_INVALID, nullptr}};
  mpv_render_context_render(owner->renderer, params);
  // Both contexts use the same texture. Complete writes before Qt composes it.
  fn->glFinish();
  renderContext->doneCurrent();
  makeCurrent();
  auto *qt = context()->extraFunctions();
  qt->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
  qt->glViewport(0, 0, pixels.width(), pixels.height());
  qt->glDisable(GL_BLEND);
  qt->glDisable(GL_DEPTH_TEST);
  qt->glDisable(GL_SCISSOR_TEST);
  blitter->bind();
  blitter->blit(texture, QMatrix4x4(), QOpenGLTextureBlitter::OriginBottomLeft);
  blitter->release();
  qt->glFinish();
  owner->recordRender(timer.nsecsElapsed());
}
void GlSurface::release() {
  if (blitter && context() && context()->isValid()) {
    makeCurrent();
    blitter->destroy();
    blitter.reset();
    doneCurrent();
  }
  if (!renderContext || !renderSurface)
    return;
  if (renderContext->makeCurrent(renderSurface.get())) {
    if (owner->renderer) {
      mpv_render_context_set_update_callback(owner->renderer, nullptr, nullptr);
      mpv_render_context_free(owner->renderer);
      owner->renderer = nullptr;
    }
    if (framebuffer) {
      renderContext->extraFunctions()->glDeleteFramebuffers(1, &framebuffer);
      framebuffer = 0;
    }
    if (texture) {
      renderContext->extraFunctions()->glDeleteTextures(1, &texture);
      texture = 0;
      textureSize = {};
    }
    renderContext->doneCurrent();
  }
}

void GlSurface::consumeHiddenFrame() {
  if (!owner->renderer || !renderContext || !renderSurface ||
      !renderContext->makeCurrent(renderSurface.get()))
    return;
  int skip = 1, block = 0;
  mpv_render_param params[] = {{MPV_RENDER_PARAM_SKIP_RENDERING, &skip},
                               {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                               {MPV_RENDER_PARAM_INVALID, nullptr}};
  mpv_render_context_render(owner->renderer, params);
  mpv_render_context_report_swap(owner->renderer);
  renderContext->doneCurrent();
}
