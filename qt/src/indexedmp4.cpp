#include "indexedmp4.h"
#include <cstring>

namespace {
using harvester::AddTime;
using harvester::BoxData;
using harvester::Time100ns;
constexpr uint32_t MaxSamples = 2000000;
void require(bool condition, const char *error) {
  if (!condition)
    throw std::runtime_error(error);
}
uint32_t version(BoxData box) {
  const auto value = box.U32(0) >> 24;
  require(value <= 1, "Unsupported MP4 clock version");
  return value;
}
int64_t editOffset(BoxData track, uint32_t movieScale, uint32_t scale,
                   int64_t *duration) {
  auto edits = track.One("edts", true);
  if (!edits.data)
    return 0;
  auto list = edits.One("elst");
  const auto ver = version(list), count = list.U32(4);
  const uint32_t width = ver ? 20 : 12;
  require(count > 0 && count <= 2 && list.size == 8 + width * count,
          "Unsupported MP4 edit sequence");
  int64_t delay = 0, offset = 0, end = 0;
  bool mediaSeen = false;
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = 8 + i * width;
    const uint64_t rawDuration = ver ? list.U64(at) : list.U32(at);
    require(rawDuration <= uint64_t(INT64_MAX), "MP4 edit clock overflow");
    const int64_t segment = Time100ns(int64_t(rawDuration), movieScale);
    const int64_t time =
        ver ? int64_t(list.U64(at + 8)) : int32_t(list.U32(at + 4));
    require(segment > 0 && list.U32(at + (ver ? 16 : 8)) == 0x10000,
            "Unsupported MP4 edit duration/rate");
    end = AddTime(end, segment);
    if (time == -1 && !mediaSeen)
      delay = AddTime(delay, segment);
    else if (time >= 0 && !mediaSeen) {
      offset = AddTime(delay, -Time100ns(time, scale));
      mediaSeen = true;
    } else
      throw std::runtime_error("Unsupported MP4 edit ordering");
  }
  require(mediaSeen, "MP4 contains only an empty edit");
  *duration = end;
  return offset;
}
} // namespace

void IndexedMp4::open(const harvester::FragmentIndex::Read &read,
                      uint64_t prefix, uint64_t total, int kind,
                      int64_t timestampOffset) {
  entries.clear();
  length = 0;
  require(total > 0 && prefix <= total,
          "Indexed MP4 needs a known total length");
  uint64_t position = 0, payloadStart = 0, payloadEnd = 0;
  bool ftyp = false;
  std::vector<uint8_t> metadata;
  while (position < prefix) {
    uint8_t header[16]{};
    require(harvester::RangeReady(position, 8, prefix) &&
                read(position, header, 8),
            "Unpublished MP4 initialization header");
    BoxData bytes{header, sizeof(header)};
    uint64_t size = bytes.U32(0);
    const uint32_t type = bytes.U32(4);
    size_t width = 8;
    if (size == 1) {
      require(harvester::RangeReady(position, 16, prefix) &&
                  read(position + 8, header + 8, 8),
              "Unpublished extended MP4 header");
      size = bytes.U64(8);
      width = 16;
    }
    if (size == 0 && type == harvester::FourCC("mdat"))
      size = total - position;
    require(size >= width && size <= total - position,
            "Invalid MP4 initialization bounds");
    if (type == harvester::FourCC("mdat")) {
      require(ftyp && !metadata.empty(), "MP4 moov must precede media data");
      payloadStart = position + width;
      payloadEnd = position + size;
      break;
    }
    require(type != harvester::FourCC("moof") &&
                harvester::RangeReady(position, size, prefix),
            "Unpublished or fragmented MP4 metadata");
    if (type == harvester::FourCC("ftyp")) {
      require(position == 0 && size <= 4096, "Invalid MP4 file-type header");
      ftyp = true;
    }
    if (type == harvester::FourCC("moov")) {
      require(ftyp && metadata.empty() && size <= 64 * 1024 * 1024,
              "Repeated or oversized MP4 moov");
      metadata.resize(size_t(size - width));
      require(read(position + width, metadata.data(), metadata.size()),
              "MP4 metadata I/O failed");
    }
    position += size;
  }
  require(payloadEnd > payloadStart, "MP4 lacks an available media header");
  BoxData movie{metadata.data(), metadata.size()};
  require(!movie.One("mvex", true).data, "Fragmented MP4 marked as indexed");
  auto mvhd = movie.One("mvhd"), track = movie.One("trak");
  auto media = track.One("mdia"), mdhd = media.One("mdhd");
  const auto movieVer = version(mvhd), mediaVer = version(mdhd);
  const auto movieScale = mvhd.U32(movieVer ? 20 : 12);
  const auto scale = mdhd.U32(mediaVer ? 20 : 12);
  const auto rawDuration = mediaVer ? mdhd.U64(24) : mdhd.U32(16);
  require(movieScale && scale && rawDuration > 0 &&
              rawDuration <= uint64_t(INT64_MAX),
          "Invalid MP4 track clock");
  require(media.One("hdlr").U32(8) == harvester::FourCC(kind ? "soun" : "vide"),
          "Wrong MP4 track type");
  length = Time100ns(int64_t(rawDuration), scale);
  const int64_t edit =
      AddTime(editOffset(track, movieScale, scale, &length), -timestampOffset);
  length = AddTime(length, -timestampOffset);
  require(length > 0, "Non-positive MP4 timeline");
  const auto table = media.One("minf").One("stbl");
  auto description = table.One("stsd");
  require(description.U32(4) == 1,
          "Multiple MP4 sample descriptions unsupported");
  description.Slice(8, description.size - 8).One(kind ? "mp4a" : "avc1");

  const auto sizes = table.One("stsz");
  const uint32_t fixed = sizes.U32(4), count = sizes.U32(8);
  require(count > 0 && count <= MaxSamples &&
              sizes.size == 12 + (fixed ? 0 : size_t(count) * 4),
          "Invalid MP4 sample-size table");
  std::vector<harvester::Sample> samples(count);
  for (uint32_t i = 0; i < count; ++i) {
    samples[i].size = fixed ? fixed : sizes.U32(12 + size_t(i) * 4);
    require(samples[i].size > 0 && samples[i].size <= 64 * 1024 * 1024,
            "Invalid MP4 packet size");
    samples[i].sync = kind != 0;
  }
  const auto times = table.One("stts");
  const uint32_t timeCount = times.U32(4);
  require(timeCount > 0 && timeCount <= count &&
              times.size == 8 + size_t(timeCount) * 8,
          "Invalid MP4 decode-time table");
  uint32_t index = 0;
  int64_t ticks = 0;
  for (uint32_t run = 0; run < timeCount; ++run) {
    const uint32_t n = times.U32(8 + size_t(run) * 8);
    const uint32_t delta = times.U32(12 + size_t(run) * 8);
    require(n > 0 && n <= count - index && delta > 0, "Invalid MP4 timing run");
    for (uint32_t j = 0; j < n; ++j) {
      auto &sample = samples[index++];
      sample.decode = sample.start = AddTime(Time100ns(ticks, scale), edit);
      ticks = AddTime(ticks, delta);
      sample.end = AddTime(Time100ns(ticks, scale), edit);
    }
  }
  require(index == count && uint64_t(ticks) == rawDuration,
          "MP4 samples do not cover declared media duration");
  const auto composition = table.One("ctts", true);
  if (composition.data) {
    const auto ver = version(composition), runs = composition.U32(4);
    require(runs > 0 && runs <= count &&
                composition.size == 8 + size_t(runs) * 8,
            "Invalid MP4 composition table");
    index = 0;
    for (uint32_t run = 0; run < runs; ++run) {
      const uint32_t n = composition.U32(8 + size_t(run) * 8);
      const uint32_t encoded = composition.U32(12 + size_t(run) * 8);
      const int64_t offset =
          Time100ns(ver ? int64_t(int32_t(encoded)) : int64_t(encoded), scale);
      require(n > 0 && n <= count - index, "Invalid MP4 composition run");
      for (uint32_t j = 0; j < n; ++j) {
        auto &sample = samples[index++];
        sample.start = AddTime(sample.start, offset);
        sample.end = AddTime(sample.end, offset);
      }
    }
    require(index == count, "Incomplete MP4 composition table");
  }
  const auto sync = table.One("stss", true);
  if (!kind) {
    if (sync.data) {
      const uint32_t n = sync.U32(4);
      require(n > 0 && n <= count && sync.size == 8 + size_t(n) * 4,
              "Invalid MP4 keyframe table");
      uint32_t previous = 0;
      for (uint32_t i = 0; i < n; ++i) {
        const uint32_t number = sync.U32(8 + size_t(i) * 4);
        require(number > previous && number <= count,
                "Invalid MP4 keyframe order");
        samples[number - 1].sync = true;
        previous = number;
      }
    } else {
      for (auto &sample : samples)
        sample.sync = true;
    }
    require(samples.front().sync, "MP4 starts without a keyframe");
  }
  const auto map = table.One("stsc");
  const auto narrow = table.One("stco", true), wide = table.One("co64", true);
  require(bool(narrow.data) != bool(wide.data),
          "MP4 requires one chunk-offset table");
  const auto offsets = narrow.data ? narrow : wide;
  const uint32_t chunkCount = offsets.U32(4), runs = map.U32(4);
  require(chunkCount > 0 && chunkCount <= count && runs > 0 &&
              runs <= chunkCount &&
              offsets.size == 8 + size_t(chunkCount) * (wide.data ? 8 : 4) &&
              map.size == 8 + size_t(runs) * 12,
          "Invalid MP4 chunk table");
  uint32_t previous = 0;
  for (uint32_t i = 0; i < runs; ++i) {
    const uint32_t first = map.U32(8 + size_t(i) * 12);
    require(first > previous && first <= chunkCount &&
                (!i ? first == 1 : true) && map.U32(12 + size_t(i) * 12) > 0 &&
                map.U32(16 + size_t(i) * 12) == 1,
            "Invalid MP4 chunk mapping");
    previous = first;
  }
  index = 0;
  uint32_t run = 0;
  uint64_t previousEnd = payloadStart;
  for (uint32_t chunk = 1; chunk <= chunkCount; ++chunk) {
    if (run + 1 < runs && chunk == map.U32(8 + size_t(run + 1) * 12))
      ++run;
    const uint32_t n = map.U32(12 + size_t(run) * 12);
    uint64_t at = wide.data ? offsets.U64(8 + size_t(chunk - 1) * 8)
                            : offsets.U32(8 + size_t(chunk - 1) * 4);
    require(n <= count - index && at >= previousEnd,
            "Overlapping or unordered MP4 chunks");
    for (uint32_t j = 0; j < n; ++j) {
      auto &sample = samples[index++];
      require(at >= payloadStart &&
                  harvester::RangeReady(at, sample.size, payloadEnd),
              "MP4 packet outside media payload");
      sample.offset = at;
      at += sample.size;
    }
    previousEnd = at;
  }
  require(index == count && harvester::RangeReady(samples.front().offset,
                                                  samples.front().size, prefix),
          "MP4 first packet is unpublished or chunk mapping is incomplete");
  entries = std::move(samples);
}
