#pragma once
#include "../../shared/progressiveretirement.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <mpv/client.h>
#include <mpv/render.h>
class Player;
class ProgressiveStreams;
class ProgressiveSource;
class StdinStream;
struct EngineRetirementBusy:std::runtime_error {
  EngineRetirementBusy():std::runtime_error("Player engines are still active or closing; retry after actual release"){}
};
struct EngineRetirementPolicy {
  static constexpr size_t Capacity=16, WorkerLimit=2;
  using Busy=EngineRetirementBusy;
};
using EngineRetirement=foxicmp::progressive::ObjectRetirement<EngineRetirementPolicy>;
// Stable callback userdata and stream factories outlive the QWidget and remain
// alive until libmpv has returned from native destruction. No GUI ownership here.
struct MpvLifetime {
  MpvLifetime();
  ~MpvLifetime();
  static std::shared_ptr<MpvLifetime> create();
  void requestStop() noexcept;
  bool initialized=false;
  mpv_handle* handle=nullptr;
  mpv_render_context* softwareRenderer=nullptr;
  std::unique_ptr<ProgressiveStreams> streams;
  std::shared_ptr<ProgressiveSource> retainedSource;
  std::unique_ptr<StdinStream> stdinStream;
  std::mutex callbackMutex;
  Player* owner=nullptr;
  std::atomic<bool> redrawPending{false};
  std::shared_ptr<std::atomic<bool>> closed=std::make_shared<std::atomic<bool>>(false);
  // Private friend fixtures set this before ownership is retired.
  std::function<void()> retirementHook;
};
