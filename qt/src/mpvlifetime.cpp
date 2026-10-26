#include "mpvlifetime.h"
#include "progressivesource.h"
#include "stdinstream.h"
MpvLifetime::MpvLifetime() {
  handle=mpv_create();if(!handle)throw std::runtime_error("libmpv: mpv_create failed");
}
void MpvLifetime::requestStop() noexcept {
  if(initialized && handle){const char* args[]={"stop",nullptr};mpv_command_async(handle,0,args);}
}
MpvLifetime::~MpvLifetime() {
  if(retirementHook)retirementHook();
  if(softwareRenderer){mpv_render_context_set_update_callback(softwareRenderer,nullptr,nullptr);mpv_render_context_free(softwareRenderer);softwareRenderer=nullptr;}
  // The render context is gone before any non-render client API teardown.
  if(handle)mpv_set_wakeup_callback(handle,nullptr,nullptr);
  if(handle){mpv_terminate_destroy(handle);handle=nullptr;}
  // mpv can no longer enter a registered stream factory or read callback.
  stdinStream.reset();streams.reset();retainedSource.reset();
  closed->store(true);
}
std::shared_ptr<MpvLifetime> MpvLifetime::create() {
  return EngineRetirement::create<MpvLifetime>([]{return new MpvLifetime;});
}
