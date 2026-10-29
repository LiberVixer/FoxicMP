#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

// Runtime IDs belong to the backend; progressive indexes belong to the source.
inline bool progressiveAudioTrackAllowed(const QVariantMap& runtime,const QJsonArray& media) {
  if(runtime.value("type")!="audio" || media.isEmpty())return true;
  const auto path=runtime.value("external-filename").toString();
  if(!path.startsWith("foxicmp-session://"))return true;
  for(const auto& entry:media) {
    const auto track=entry.toObject();const int index=track.value("index").toInt();
    if(index>0 && path.endsWith(index==1?QStringLiteral("/audio"):QStringLiteral("/track/")+QString::number(index)))
      return track.value("ready").toBool() && track.value("errorCode").toString().isEmpty();
  }
  return false;
}
