// Private IPC fixture: production Window/ProgressiveApi and decoded null audio.
// No installed profile, desktop routing or physical audio endpoint is changed.
#include "window.h"
#include "player.h"
#include "profile.h"
#include "progressiveapi.h"
#include "windowsruntime.h"
#include <QtWidgets>
#include <cstdio>
int main(int argc,char** argv)try{
#ifdef Q_OS_WIN
 WindowsRuntime runtime;if(!runtime.valid())return 1;
#endif
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
 const auto args=app.arguments();const auto at=args.indexOf("--profile");
 if(at<0 || at+1>=args.size())return 2;
 const auto directory=QFileInfo(args[at+1]).absoluteFilePath();
 if(args.contains("--progressive-api-info")){
  const auto json=QJsonDocument(ProgressiveApi::information(directory)).toJson(QJsonDocument::Compact);
  std::fwrite(json.constData(),1,json.size(),stdout);return 0;
 }
 Profile profile(directory);
 {
  Window window(profile);auto* player=window.findChild<Player*>();
  if(!player || !player->set("ao","null"))return 3;
  ProgressiveApi api(&window);if(!api.listen(directory))return 4;
  window.show();const auto code=app.exec();if(code)return code;
 }
 return ProgressiveApi::settleContinuousExit()?0:5;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
