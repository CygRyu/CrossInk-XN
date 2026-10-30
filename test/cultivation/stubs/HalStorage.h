// Copy of test/book_reading_stats_atomic/stubs/HalStorage.h plus mkdir() and bytesOf(),
// and (for PackStore) folders, seek(), file names and modification times,
// and (for the Chronicle) open() with O_CREAT / O_APPEND.
#pragma once

#include <Print.h>
#include <fcntl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct HostFileData {
  std::vector<uint8_t> bytes;
  uint32_t modified = 0;  // packed FAT date/time stand-in
};

class HalFile;
struct HostDirEntry {
  std::string name;
  std::shared_ptr<HostFileData> data;  // null for a folder
};

class HalFile : public Print {
 public:
  HalFile() = default;
  explicit HalFile(std::shared_ptr<HostFileData> data) : data_(std::move(data)) {}

  int read(void* output, const size_t length) {
    if (!data_) return 0;
    const size_t readable = std::min(length, data_->bytes.size() - std::min(cursor_, data_->bytes.size()));
    if (readable > 0) std::copy_n(data_->bytes.data() + cursor_, readable, static_cast<uint8_t*>(output));
    cursor_ += readable;
    return static_cast<int>(readable);
  }

  size_t write(const uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* input, const size_t length) override {
    if (!data_) return 0;
    if (cursor_ + length > data_->bytes.size()) data_->bytes.resize(cursor_ + length);
    std::copy_n(input, length, data_->bytes.begin() + static_cast<std::ptrdiff_t>(cursor_));
    cursor_ += length;
    return length;
  }
  size_t write(const void* input, const size_t length) { return write(static_cast<const uint8_t*>(input), length); }

  void flush() {}
  bool sync() const { return static_cast<bool>(data_); }
  bool close() {
    data_.reset();
    cursor_ = 0;
    return true;
  }
  size_t fileSize() const { return data_ ? data_->bytes.size() : 0; }
  explicit operator bool() const { return static_cast<bool>(data_) || isDir_; }

  bool seek(const size_t pos) {
    if (!data_ || pos > data_->bytes.size()) return false;
    cursor_ = pos;
    return true;
  }
  uint32_t modificationTime() const { return data_ ? data_->modified : 0; }
  bool isDirectory() const { return isDir_; }
  void rewindDirectory() { dirPos_ = 0; }
  size_t getName(char* out, const size_t len) const {
    snprintf(out, len, "%s", name_.c_str());
    return strlen(out);
  }
  HalFile openNextFile() {
    HalFile f;
    if (!isDir_ || dirPos_ >= entries_.size()) return f;
    const HostDirEntry& e = entries_[dirPos_++];
    f.name_ = e.name;
    f.data_ = e.data;
    f.isDir_ = !e.data;
    return f;
  }

  // Used by HalStorage::open().
  static HalFile file(std::shared_ptr<HostFileData> data, std::string name) {
    HalFile f(std::move(data));
    f.name_ = std::move(name);
    return f;
  }
  static HalFile folder(std::vector<HostDirEntry> entries) {
    HalFile f;
    f.isDir_ = true;
    f.entries_ = std::move(entries);
    return f;
  }

 private:
  std::shared_ptr<HostFileData> data_;
  size_t cursor_ = 0;
  bool isDir_ = false;
  std::string name_;
  std::vector<HostDirEntry> entries_;
  size_t dirPos_ = 0;
};

using FsFile = HalFile;

class HalStorage {
 public:
  void reset() {
    files_.clear();
    failRenameFrom_.clear();
    failRemovePath_.clear();
  }

  bool mkdir(const char*, bool = true) { return true; }

  // A file, or a folder listing its direct children (folders exist when some
  // file lies below them).
  HalFile open(const char* path) {
    const std::string p = path;
    const auto found = files_.find(p);
    if (found != files_.end()) return HalFile::file(found->second, p.substr(p.rfind('/') + 1));
    const std::string prefix = p + "/";
    std::vector<HostDirEntry> entries;
    std::unordered_set<std::string> folders;
    for (const auto& [name, data] : files_) {
      if (name.compare(0, prefix.size(), prefix) != 0) continue;
      const std::string rest = name.substr(prefix.size());
      const size_t slash = rest.find('/');
      if (slash == std::string::npos) {
        entries.push_back({rest, data});
      } else if (folders.insert(rest.substr(0, slash)).second) {
        entries.push_back({rest.substr(0, slash), nullptr});
      }
    }
    if (entries.empty()) return HalFile{};
    return HalFile::folder(std::move(entries));
  }

  // Opening with flags, as the device's SdFat open() does: O_CREAT makes a
  // missing file, O_TRUNC empties it, O_APPEND starts writing at its end.
  HalFile open(const char* path, const int oflag) {
    const std::string p = path;
    auto found = files_.find(p);
    if (found == files_.end()) {
      if ((oflag & O_CREAT) == 0) return HalFile{};
      found = files_.emplace(p, std::make_shared<HostFileData>()).first;
    }
    if ((oflag & O_TRUNC) != 0) found->second->bytes.clear();
    HalFile f = HalFile::file(found->second, p.substr(p.rfind('/') + 1));
    if ((oflag & O_APPEND) != 0) f.seek(found->second->bytes.size());
    return f;
  }

  // Test helper: write a whole file with a modification time.
  void put(const std::string& path, const std::string& text, const uint32_t modified = 1) {
    auto data = std::make_shared<HostFileData>();
    data->bytes.assign(text.begin(), text.end());
    data->modified = modified;
    files_[path] = std::move(data);
  }
  bool exists(const char* path) const { return files_.contains(path); }
  bool remove(const char* path) {
    if (failRemovePath_ == path) {
      failRemovePath_.clear();
      return false;
    }
    return files_.erase(path) > 0;
  }

  bool rename(const char* from, const char* to) {
    if (failRenameFrom_.erase(from) > 0) {
      return false;
    }
    const auto source = files_.find(from);
    if (source == files_.end() || files_.contains(to)) return false;
    files_[to] = source->second;
    files_.erase(source);
    return true;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    const auto found = files_.find(path);
    if (found == files_.end()) return false;
    file = HalFile(found->second);
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    auto data = std::make_shared<HostFileData>();
    files_[path] = data;
    file = HalFile(std::move(data));
    return true;
  }

  // Test helpers: direct access to a file's bytes.
  std::vector<uint8_t>* bytesOf(const std::string& path) {
    const auto found = files_.find(path);
    return found == files_.end() ? nullptr : &found->second->bytes;
  }

  void failNextRenameFrom(std::string path) { failRenameFrom_.insert(std::move(path)); }
  void failNextRemove(std::string path) { failRemovePath_ = std::move(path); }

 private:
  std::unordered_map<std::string, std::shared_ptr<HostFileData>> files_;
  std::unordered_set<std::string> failRenameFrom_;
  std::string failRemovePath_;
};

inline HalStorage Storage;
