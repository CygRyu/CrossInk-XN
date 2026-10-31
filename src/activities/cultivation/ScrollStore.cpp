#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "GameState.h"
#include "ScrollStore.h"

namespace cultivation {
namespace scrollstore {
namespace {

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void putLe32(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

bool appendBytes(const char* path, const uint8_t* data, const size_t len) {
  Storage.mkdir(save::kDirectory);
  FsFile f = Storage.open(path, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR("CULT", "Could not open %s for appending", path);
    return false;
  }
  const bool ok = f.write(data, len) == len;
  f.close();
  if (!ok) {
    LOG_ERR("CULT", "Could not append to %s", path);
  }
  return ok;
}

}  // namespace

uint32_t titleHash(const char* title) {
  uint32_t h = 2166136261u;
  for (const char* p = title; p != nullptr && *p != '\0'; ++p) {
    h ^= static_cast<uint8_t>(*p);
    h *= 16777619u;
  }
  return h != 0 ? h : 1u;
}

PendingRecord recordFor(const char* title, const uint32_t seconds, const uint32_t buckets[4],
                        const float progressPercent, const bool completed) {
  PendingRecord r;
  r.completed = completed;
  r.book.titleHash = titleHash(title);
  r.book.seconds = seconds;
  const Element e = scrolls::elementForBuckets(buckets);
  switch (e) {
    case Element::Wood:
      r.book.bucket = 0;
      break;
    case Element::Fire:
      r.book.bucket = 1;
      break;
    case Element::Metal:
      r.book.bucket = 2;
      break;
    case Element::Water:
      r.book.bucket = 3;
      break;
    default:
      r.book.bucket = 0xFF;
      break;
  }
  if (progressPercent < 0.0f) {
    r.book.shareRead = 75;
  } else if (progressPercent >= 99.5f) {
    r.book.shareRead = 100;
  } else {
    r.book.shareRead = static_cast<uint8_t>(progressPercent + 0.5f);
  }
  return r;
}

void encode(const PendingRecord& r, uint8_t out[kRecordSize]) {
  putLe32(out, r.book.titleHash);
  putLe32(out + 4, r.book.seconds);
  out[8] = r.book.bucket;
  out[9] = r.book.shareRead;
  out[10] = r.completed ? 1 : 0;
  out[11] = kRecordFormat;
}

bool decode(const uint8_t in[kRecordSize], PendingRecord& r) {
  if (in[11] != kRecordFormat) return false;
  r.book.titleHash = le32(in);
  r.book.seconds = le32(in + 4);
  r.book.bucket = in[8] < 4 ? in[8] : 0xFF;
  r.book.shareRead = in[9] <= 100 ? in[9] : 100;
  r.completed = in[10] != 0;
  return r.book.titleHash != 0;
}

bool append(const PendingRecord& r) {
  uint8_t bytes[kRecordSize];
  encode(r, bytes);
  return appendBytes(kPendingPath, bytes, sizeof(bytes));
}

uint8_t readPending(scrolls::BookRecord* out, const uint8_t max, bool& overflow) {
  overflow = false;
  if (Storage.exists(kPendingRestPath)) Storage.remove(kPendingRestPath);
  FsFile f;
  if (!Storage.openFileForRead("CULT", kPendingPath, f)) return 0;
  FsFile rest;
  uint8_t n = 0;
  bool full = false;  // from the first book that did not fit, every other book waits
  uint8_t bytes[kRecordSize];
  while (f.read(bytes, kRecordSize) == static_cast<int>(kRecordSize)) {
    PendingRecord r;
    if (!decode(bytes, r)) continue;  // a damaged or future record: skipped
    int at = -1;
    for (uint8_t i = 0; i < n; ++i) {
      if (out[i].titleHash == r.book.titleHash) at = i;
    }
    if (at < 0 && (full || (r.completed && n >= max))) {
      full = true;
      overflow = true;
      if (!rest) rest = Storage.open(kPendingRestPath, O_WRONLY | O_CREAT | O_TRUNC);
      if (rest) rest.write(bytes, kRecordSize);
      continue;
    }
    if (!r.completed) {  // unmarked before the game minted it: nothing to mint
      if (at >= 0) {
        memmove(out + at, out + at + 1, static_cast<size_t>(n - at - 1) * sizeof(out[0]));
        --n;
      }
    } else if (at >= 0) {
      out[at] = r.book;  // marked again: the latest numbers
    } else {
      out[n++] = r.book;
    }
  }
  f.close();
  if (rest) rest.close();
  if (overflow) {
    LOG_INF("CULT", "More than %u finished books were waiting; the rest wait for the next visit", max);
  }
  return n;
}

bool hasPending() { return Storage.exists(kPendingPath); }

bool finishPending() {
  if (Storage.exists(kPendingPath) && !Storage.remove(kPendingPath)) return false;
  if (!Storage.exists(kPendingRestPath)) return true;
  return Storage.rename(kPendingRestPath, kPendingPath);
}

bool isMinted(const uint32_t hash) {
  FsFile f;
  if (!Storage.openFileForRead("CULT", kMintedPath, f)) return false;
  uint8_t bytes[4];
  bool found = false;
  while (!found && f.read(bytes, 4) == 4) found = le32(bytes) == hash;
  f.close();
  return found;
}

bool addMinted(const uint32_t* hashes, const uint8_t n) {
  bool ok = true;
  for (uint8_t i = 0; i < n; ++i) {
    if (isMinted(hashes[i])) continue;
    uint8_t bytes[4];
    putLe32(bytes, hashes[i]);
    ok = appendBytes(kMintedPath, bytes, sizeof(bytes)) && ok;
  }
  return ok;
}

void clearForNewLife() {
  if (Storage.exists(kPendingPath)) Storage.remove(kPendingPath);
  if (Storage.exists(kPendingRestPath)) Storage.remove(kPendingRestPath);
  if (Storage.exists(kMintedPath)) Storage.remove(kMintedPath);
}

}  // namespace scrollstore
}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
