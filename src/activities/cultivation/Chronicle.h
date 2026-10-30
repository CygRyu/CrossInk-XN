#pragma once

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

// The age in a season line ("Spring, age 17. ..."), or -1 if `line` is not one.
int32_t ageInLine(const char* line);

}  // namespace chronicle
}  // namespace cultivation
