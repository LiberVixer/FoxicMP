// Capture the real Player audio adapter, with no audio hardware. GPL-3.0-or-later.
#include "player.h"
#include "windowsruntime.h"
#include "mpvlifetime.h"
#include <QtWidgets>
#include <chrono>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
  WindowsRuntime runtime;
  if (!runtime.valid()) return 2;
#endif
  QApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 5 && args.size() != 6 && args.size() != 8 && args.size() != 9) return 2;
  const int rate = args[3].toInt(), channels = args[4].toInt();
  if (rate < 8000 || rate > 384000 || (channels != 1 && channels != 2)) return 2;
  AudioPcmWindow window;
  if (args.size() >= 8) {
    bool beginOk = false, endOk = false;
    window = {args[5].toULongLong(&beginOk), args[6].toULongLong(&endOk), rate};
    if (!beginOk || !endOk || !window.valid()) return 2;
  }
  const bool stopReplace = args.size() == 9 && args[8] == "stop-replace";
  const bool replaceGain = args.size() == 9 && args[8] == "replace-gain";
  const bool replace = stopReplace || replaceGain || (args.size() == 9 && args[8] == "replace");
  const QString graph = args.size() == 6 ? args[5]
                        : replaceGain ? "volume=0.5"
                        : args.size() == 9 && !replace ? args[8] : "anull";
  QJsonArray captures;
  QString failure;
  {
    Player player(nullptr, "software", {{"ao", "pcm"}, {"ao-pcm-file", args[2]},
      {"ao-pcm-waveheader", "no"}, {"audio-format", "s16"},
      {"audio-samplerate", QString::number(rate)},
      {"audio-channels", channels == 1 ? "mono" : "stereo"},
      {"audio-display", "no"}, {"gapless-audio", "no"}, {"hwdec", "no"}});
    player.resize(160, 90);
    player.show();
    player.set("volume", 100);
    player.set("mute", false);
    if (!player.setAudioGraph(replaceGain ? "anull" : graph, "s16", rate)) return 3;
    // Invalid requests must leave the managed filters and current source alone.
    const auto before = player.get("af");
    if (player.loadPcmWindowSource(args[1], {}, {5, 5, rate}) ||
        player.loadPcmWindowSource(args[1], {}, {0, 10, 0}) ||
        player.loadPcmWindowSource(args[1], {{"af-add", "anull"}}, {0, 10, rate}) ||
        player.loadPcmWindowSource(args[1], {}, {0, 9007199254740992ULL, rate}) ||
        player.get("af") != before) return 4;
    for (int pass = 0; pass < (replace ? 3 : 1); ++pass) {
      const bool trimmed = window.enabled() && pass == 0;
      QEventLoop loop;
      bool natural = false, loaded = false;
      QTimer deadline;
      deadline.setSingleShot(true);
      QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        failure = "Player PCM capture timed out"; loop.quit();
      });
      const auto errored = QObject::connect(&player, &Player::error, &loop, [&](const QString& text) {
        failure = text; loop.quit();
      });
      const auto ended = QObject::connect(&player, &Player::ended, &loop, [&](bool eof) {
        natural = eof; loop.quit();
      });
      const auto opened = QObject::connect(&player, &Player::loaded, &loop, [&] {
        loaded = true;
        // Reconfiguration must retain trim before user processing/resampling.
        if (!player.setAudioGraph(graph, "s16", rate)) { loop.quit(); return; }
        QStringList labels;
        for (const auto& f : player.get("af").toList()) labels << f.toMap().value("label").toString();
        if (labels.contains("foxicmp-pcm-window") != trimmed ||
            !labels.contains("foxicmp-audio") || !labels.contains("foxicmp-audio-format")) {
          failure = "Wrong PCM/user filter ownership"; loop.quit(); return;
        }
        captures.append(QJsonObject{{"pass", pass}, {"trimmed", trimmed},
                                    {"filterLabels", QJsonArray::fromStringList(labels)}});
        if (stopReplace && pass == 0) player.command({"stop"});
        else player.set("pause", false);
      });
      QMap<QString, QString> options{{"pause", "yes"}, {"rebase-start-time", "no"}};
      if (trimmed) {
        options["start"] = args[7];
        options["hr-seek"] = "yes";
        options["hr-seek-demuxer-offset"] = "0.25";
      }
      const bool accepted = trimmed ? player.loadPcmWindowSource(args[1], options, window)
                            : stopReplace && pass == 1 ? player.command({"loadfile", args[1], "replace"})
                            : player.loadSource(args[1], options);
      if (!accepted) { failure = "Player rejected PCM load"; break; }
      deadline.start(30000);
      loop.exec();
      QObject::disconnect(opened); QObject::disconnect(ended); QObject::disconnect(errored);
      if (!loaded || (!natural && !(stopReplace && pass == 0)) || !failure.isEmpty()) {
        if (failure.isEmpty()) failure = "Player did not reach natural EOF";
        break;
      }
    }
  }
  if (!EngineRetirement::waitEmpty(std::chrono::seconds(5))) return 5;
  if (!failure.isEmpty()) { std::cerr << failure.toStdString() << '\n'; return 1; }
  std::cout << QJsonDocument(QJsonObject{{"result", "PASS"}, {"captures", captures},
                                       {"audioDeviceOpened", false}}).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}
