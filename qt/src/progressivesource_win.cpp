#include "progressivesource.h"
#include "progressivefile.h"
#include <stdexcept>

// The version-1 published-prefix API is common. The older Linux provider/lease
// transport remains a distinct compatibility path, not a Windows named pipe.
std::shared_ptr<ProgressiveSource> ProgressiveSource::open(const QString &) {
  throw std::runtime_error("Use the published-prefix progressive API on Windows");
}
ProgressiveSource::~ProgressiveSource() {
  cancel();
  if (worker.joinable()) worker.join();
  for (auto file : files) if (file != -1) ProgressiveFile::close(file);
}
