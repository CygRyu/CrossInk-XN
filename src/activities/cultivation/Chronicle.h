#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>

#include "GameState.h"
#include "Grammar.h"
#include "LifeLog.h"

// The Chronicle (DOC: "The Chronicle"): /.crosspoint/cultivation/chronicle.txt,
// a plain-text book of every life, newest at the bottom.
//
//   The Chronicle of Lin Yue
//
//   Life 1: Lin Yue
//
//   <birth line> <Awakening Test scene> <Past Life Memories>
//
//   Age 16
//   Spring, age 16. ...
//   Summer, age 16. ...
//
// Lines are only ever appended: rewriting the file on every visit would be
// slow on the SD card and could damage it if the power went mid-write.
//
// The file is written before the save. If the save then fails, the next visit
// counts the same reading again and builds the same lines (they are seeded by
// season); appendVisit() sees that the file already ends with them and skips
// them, so nothing is written twice.
//
// Memory: a 256-byte stack buffer for the file's tail; nothing on the heap.
// The viewer (v0.2.3) opens on the newest page; its titles and year jumps use
// the "Age N" headings, so keep writing one whenever the age changes.
namespace cultivation {
namespace chronicle {

constexpr char kPath[] = "/.crosspoint/cultivation/chronicle.txt";

bool exists();

// Appends the opening chapter of the character's current life, with the
// book's title first when the file is new. Used at creation, and on the first
// visit of a character made before the Chronicle existed (the file is
// missing then). Without a grammar engine only the headings are written.
bool writeOpening(const GameState& state, Grammar* grammar);

// Appends one visit's life-log lines, with an "Age N" heading whenever the
// age changes. Does nothing for an empty log or one already written.
bool appendVisit(const lifelog::LifeLog& log);
// The same for any lines, such as one breakthrough line: written once, with
// an "Age N" heading when the age changes.
bool appendLines(const lifelog::Line* lines, uint8_t count);

// The age in a season line ("Spring, age 17. ..."), or -1 if `line` is not one.
int32_t ageInLine(const char* line);

// Export (user request): a copy of the whole Chronicle in a folder the
// player can see, "/Chronicles/The Chronicle of <name>.txt", replacing the
// previous copy. Copied in 256-byte pieces through the stack, never held in
// RAM; written to a .tmp first, so a failed copy leaves the old export. The
// name's characters that FAT cannot store (\ / : * ? " < > |) become '_'.
// Writes the path into `pathOut` and returns true on success.
constexpr char kExportDirectory[] = "/Chronicles";
bool exportCopy(const GameState& state, char* pathOut, size_t pathLen);
void exportPath(const GameState& state, char* out, size_t len);

// --- Reading helpers for the viewer (v0.2.3) -----------------------------------
// The viewer opens on the newest page and jumps by years, so it must never
// read the whole file: a long life's Chronicle can pass 500 KB. Each helper
// reads through a ~300-byte stack buffer, only near the offsets it is given.
// Page layout itself stays in the viewer (it needs the renderer). `size` is
// the open file's size.

constexpr uint32_t kNotFound = 0xFFFFFFFFu;
constexpr int32_t kJumpYears = 10;  // the jump menu's "10 years back / forward" (user, session 9)

// "Age 52" -> 52; anything else, -1. `len` bytes (no terminator needed).
int32_t headingAge(const char* text, size_t len);

// The start of the line holding `pos` (0 at the top of the file).
uint32_t lineStartAtOrBefore(FsFile& f, uint32_t size, uint32_t pos);

// The line starts in [from, to), oldest first; at most `max`, keeping the
// newest when there are more. Returns how many were written.
uint8_t lineStartsIn(FsFile& f, uint32_t size, uint32_t from, uint32_t to, uint32_t* out, uint8_t max);

// The first "Age N" heading at or after `from` with N >= minAge, or kNotFound.
uint32_t findHeadingForward(FsFile& f, uint32_t size, uint32_t from, int32_t minAge);
// The last "Age N" heading starting before `before` with N <= maxAge, or
// kNotFound. Its age goes into `ageOut` when given.
uint32_t findHeadingBackward(FsFile& f, uint32_t size, uint32_t before, int32_t maxAge, int32_t* ageOut = nullptr);

// `start` moved up to a heading ("Age N", "Life N: ...") right above it,
// blank lines between them skipped; else `start`. The viewer uses it so a
// page found going back never begins right under a heading (which would
// leave the heading at the bottom of the page before, apart from its entries).
uint32_t withHeadingAbove(FsFile& f, uint32_t size, uint32_t start);

// The age a page starting at `start` belongs to, for its title: its first
// season line or "Age N" heading (blank lines skipped), else the nearest
// heading above it. -1 for the opening chapter (birth, Awakening Test).
int32_t pageAge(FsFile& f, uint32_t size, uint32_t start);

// Where "10 years back" / "10 years forward" land from a page at `start`
// whose age is `age` (pageAge). Back lands on the nearest heading at or below
// age - 10, or the beginning (0). Forward lands on the first heading at or
// above age + 10 (counted from the starting age on the opening chapter), or
// kNotFound, meaning "the newest page".
uint32_t jumpBack(FsFile& f, uint32_t size, uint32_t start, int32_t age);
uint32_t jumpForward(FsFile& f, uint32_t size, uint32_t start, int32_t age);

}  // namespace chronicle
}  // namespace cultivation
