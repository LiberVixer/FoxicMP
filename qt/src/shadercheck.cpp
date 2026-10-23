// Isolated compiler for the editor's single-hook GLSL dialect. No user code is
// executed during validation. --probe draws only in developer tests.
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <cstdio>
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QFile input;
  input.open(stdin, QIODevice::ReadOnly);
  const auto body = input.read(65537);
  QJsonObject result;
  const auto output = [&](const QString &error) {
    result["ok"] = error.isEmpty();
    if (!error.isEmpty())
      result["error"] = error;
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
    return error.isEmpty() ? 0 : 1;
  };
  if (body.size() > 65536 || body.contains('\0') || body.contains("#") ||
      body.contains("//!"))
    return output("Unsupported source or size");
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  QOpenGLContext context;
  context.setFormat(format);
  if (!context.create())
    return output("Cannot create OpenGL context");
  QOffscreenSurface surface;
  surface.setFormat(context.format());
  surface.create();
  if (!surface.isValid() || !context.makeCurrent(&surface))
    return output("Cannot activate OpenGL context");
  auto *gl = context.extraFunctions();
  result["renderer"] = QString::fromLatin1(
      reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)));
  QOpenGLShaderProgram program;
  const QByteArray prefix =
      "#version 330 core\nuniform sampler2D tex;\nin vec2 uv;\nout vec4 "
      "result;\n#define HOOKED_pos uv\n#define HOOKED_size "
      "vec2(textureSize(tex,0))\n#define HOOKED_pt "
      "(vec2(1.0)/HOOKED_size)\n#define HOOKED_mul 1.0\n#define HOOKED_rot "
      "mat2(1.0)\n#define HOOKED_raw tex\n#define HOOKED_tex(p) "
      "texture(tex,p)\n#define HOOKED_texOff(p) "
      "texture(tex,uv+(p)*HOOKED_pt)\nuniform float random;\nuniform int "
      "frame;\nuniform vec2 input_size;\nuniform vec2 target_size;\nuniform "
      "vec2 tex_offset;\n";
  if (!program.addShaderFromSourceCode(
          QOpenGLShader::Vertex,
          "#version 330 core\nout vec2 uv;\nvoid main(){vec2 "
          "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2."
          "0-1.0,0,1);}") ||
      !program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                       prefix + body +
                                           "\nvoid main(){result=hook();}\n") ||
      !program.link())
    return output(program.log());
  if (app.arguments().contains("--probe")) {
    QOpenGLFramebufferObject fbo(16, 16);
    if (!fbo.isValid() || !fbo.bind())
      return output("Cannot create probe framebuffer");
    unsigned int texture = 0, vao = 0;
    gl->glGenTextures(1, &texture);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    const unsigned char pixel[] = {64, 128, 192, 255};
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixel);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    gl->glViewport(0, 0, 16, 16);
    program.bind();
    program.setUniformValue("tex", 0);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    gl->glFinish();
    unsigned char color[4]{};
    gl->glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color);
    result["red"] = int(color[0]);
    result["green"] = int(color[1]);
    result["blue"] = int(color[2]);
    gl->glDeleteVertexArrays(1, &vao);
    gl->glDeleteTextures(1, &texture);
  }
  return output({});
}
