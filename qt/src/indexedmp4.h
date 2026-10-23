#pragma once
#include "DSUtil/HarvesterMP4.h"

// A bounded single-track fast-start MP4 index. It reads metadata only from the
// confirmed prefix; sample offsets may describe data that has not arrived yet.
class IndexedMp4 {
public:
  void open(const harvester::FragmentIndex::Read &read, uint64_t prefix,
            uint64_t total, int kind, int64_t timestampOffset);
  const std::vector<harvester::Sample> &samples() const { return entries; }
  int64_t duration() const { return length; }

private:
  std::vector<harvester::Sample> entries;
  int64_t length = 0;
};
