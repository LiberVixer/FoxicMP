#include "instancebroker.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 4)
    return 2;
  LaunchRequest request;
  if (!LaunchRequest::decode(QByteArray::fromBase64(args[2].toLatin1()),
                             &request))
    return 3;
  InstanceBroker broker(args[1]);
  const auto result = broker.start(request, args[3] != "independent");
  std::printf("%d\n", int(result));
  std::fflush(stdout);
  if (result == InstanceBroker::Failed)
    return 1;
  if (args[3] == "hold")
    return app.exec();
  return 0;
}
