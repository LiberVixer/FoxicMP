#pragma once
#include <QOpenGLWidget>
#include <memory>
class Player;
class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLTextureBlitter;

// Keep libmpv's GL state separate from Qt's compositor. Only the color texture
// is shared; framebuffer objects belong to their individual contexts.
class GlSurface final : public QOpenGLWidget {
public:
  explicit GlSurface(Player *owner);
  ~GlSurface() override;
  void release();
  void consumeHiddenFrame();

protected:
  void initializeGL() override;
  void paintGL() override;

private:
  Player *owner;
  std::unique_ptr<QOpenGLContext> renderContext;
  std::unique_ptr<QOffscreenSurface> renderSurface;
  unsigned int framebuffer = 0, texture = 0;
  QSize textureSize;
  std::unique_ptr<QOpenGLTextureBlitter> blitter;
};
