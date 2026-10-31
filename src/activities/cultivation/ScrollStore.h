#pragma once

#include <cstddef>
#include <cstdint>

#include "Scrolls.h"

// The two small files behind Technique Scrolls (DOC: "Book finished: a hook,
// not a scan"), both in /.crosspoint/cultivation/:
//
//   pending_scrolls.bin  12-byte records appended by CultivationHooks each time
//                        a book is marked finished or unfinished; the game
//                        mints scrolls from it on the next visit and deletes it.
//   minted.bin           4-byte title hashes of every book this life has
//                        minted, so a book whose scroll was spent or sold
//                        cannot be marked finished again for another one.
//
// Both are deleted when a character is created (baseline rule: books
// finished before the character existed give nothing).
//
// Record (little-endian): title hash u32 | seconds read in the book u32 |
// dominant bucket u8 (0-3, 0xFF none over 40%) | share read u8 (0-100) |
// completed u8 (1 marked finished, 0 unmarked) | format u8 (1).
//
// Memory: records are streamed through a 12-byte stack buffer; readPending()
// fills the caller's array (the activity's, ~0.3 KB while minting).
namespace cultivation {
namespace scrollstore {

constexpr char kPendingPath[] = "/.crosspoint/cultivation/pending_scrolls.bin";
constexpr char kMintedPath[] = "/.crosspoint/cultivation/minted.bin";
// Books that did not fit in one visit, kept for the next one (same format).
constexpr char kPendingRestPath[] = "/.crosspoint/cultivation/pending_scrolls.rest";
constexpr size_t kRecordSize = 12;
constexpr uint8_t kRecordFormat = 1;
constexpr uint8_t kMaxPending = 32;  // books finished between two visits

struct PendingRecord {
  scrolls::BookRecord book;
  bool completed = true;
};

// FNV-1a over the title's bytes, never 0. A book's identity in the game.
uint32_t titleHash(const char* title);

// Builds the record for one book. `seconds` and `buckets` are the book's own
// stats (BookReadingStats); `progressPercent` is where the reader was when it
// was marked finished (negative = unknown, counted as 75%).
PendingRecord recordFor(const char* title, uint32_t seconds, const uint32_t buckets[4], float progressPercent,
                        bool completed);

void encode(const PendingRecord& r, uint8_t out[kRecordSize]);
bool decode(const uint8_t in[kRecordSize], PendingRecord& r);

bool append(const PendingRecord& r);  // the hook's one small write

// Replays the pending log in order: a mark adds or refreshes a book, a later
// unmark removes it. Writes up to `max` books in first-marked order and
// returns how many. Once `max` books are held, records of any other book go
// to kPendingRestPath unchanged (`overflow` is set): they wait for the next
// visit, so marking many books finished at once loses none.
uint8_t readPending(scrolls::BookRecord* out, uint8_t max, bool& overflow);
bool hasPending();
// After the save: the pending list becomes what was left for the next visit.
bool finishPending();

bool isMinted(uint32_t titleHash);
bool addMinted(const uint32_t* hashes, uint8_t n);

// A new life: forgets both files.
void clearForNewLife();

}  // namespace scrollstore
}  // namespace cultivation
