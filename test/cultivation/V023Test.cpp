#include <Chronicle.h>
#include <HalStorage.h>
#include <Simulation.h>
#include <gtest/gtest.h>

#include <climits>
#include <cstdio>
#include <string>
#include <vector>

// v0.2.3 (agreed with the user): the Chronicle viewer for long lives opens on
// the newest page, titles pages by age and jumps 10 years at a time; the
// stamina bank holds 20. The reading helpers are checked here against plain
// string searches over the same text, up to a 500 KB file.
using namespace cultivation;

namespace {

constexpr char kPath[] = "/.crosspoint/cultivation/chronicle.txt";

// A Chronicle like the game writes: the opening chapter, then "Age N"
// headings (with a blank line before each) and season lines. Ages listed in
// `skip` have no lines and so no heading, as when a visit spans years.
std::string makeChronicle(const int lastAge, const std::vector<int>& skip = {}, const int linesPerSeason = 1) {
  static const char* const kSeasons[4] = {"Spring", "Summer", "Autumn", "Winter"};
  std::string t = "The Chronicle of Lin Yue\n\nLife 1: Lin Yue\n\n";
  t += "Born in a village at the foot of the hills. The elder's testing stone glowed a murky green.\n";
  for (int age = kStartAgeYears; age <= lastAge; ++age) {
    bool skipped = false;
    for (const int s : skip) skipped = skipped || s == age;
    if (skipped) continue;
    t += "\nAge " + std::to_string(age) + "\n";
    for (int season = 0; season < 4; ++season) {
      for (int k = 0; k < linesPerSeason; ++k) {
        t += std::string(kSeasons[season]) + ", age " + std::to_string(age) +
             ". Old Bai of Willow Crossing sold you a bundle of herbs for three taels and a story about a black ape.\n";
      }
    }
  }
  return t;
}

struct Open {
  FsFile f;
  uint32_t size = 0;
  explicit Open(const std::string& text) {
    Storage.reset();
    Storage.put(kPath, text);
    EXPECT_TRUE(Storage.openFileForRead("TEST", kPath, f));
    size = static_cast<uint32_t>(f.fileSize());
  }
};

std::vector<uint32_t> lineStarts(const std::string& t) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < t.size(); ++i) {
    if (i == 0 || t[i - 1] == '\n') out.push_back(static_cast<uint32_t>(i));
  }
  return out;
}

int32_t headingAt(const std::string& t, const uint32_t p) {
  const size_t end = t.find('\n', p);
  const std::string line = t.substr(p, end == std::string::npos ? std::string::npos : end - p);
  return chronicle::headingAge(line.c_str(), line.size());
}

}  // namespace

TEST(ChronicleViewer, ReadsAgeHeadings) {
  EXPECT_EQ(chronicle::headingAge("Age 52", 6), 52);
  EXPECT_EQ(chronicle::headingAge("Age 52\n", 7), 52);
  EXPECT_EQ(chronicle::headingAge("Age 1200\r\n", 10), 1200);
  EXPECT_EQ(chronicle::headingAge("Age 52\nSpring", 13), 52);
  EXPECT_EQ(chronicle::headingAge("Age ", 4), -1);
  EXPECT_EQ(chronicle::headingAge("Age x", 5), -1);
  EXPECT_EQ(chronicle::headingAge("Age 5x", 6), -1);
  EXPECT_EQ(chronicle::headingAge("Spring, age 52.", 15), -1);
  EXPECT_EQ(chronicle::headingAge("Life 1: Lin Yue", 15), -1);
  EXPECT_EQ(chronicle::headingAge(nullptr, 0), -1);
}

TEST(ChronicleViewer, FindsLineStartsLikeAPlainSearch) {
  const std::string t = makeChronicle(30);
  Open o(t);
  const auto starts = lineStarts(t);
  // Every position, including the newline itself and the end of the file.
  size_t k = 0;
  for (uint32_t pos = 0; pos <= o.size; ++pos) {
    while (k + 1 < starts.size() && starts[k + 1] <= pos) ++k;
    ASSERT_EQ(chronicle::lineStartAtOrBefore(o.f, o.size, pos), pos == o.size ? starts.back() : starts[k])
        << "pos " << pos;
  }
  // The newest line starts in a window, oldest first.
  uint32_t out[48];
  const uint32_t from = o.size - 2000;
  const uint8_t n = chronicle::lineStartsIn(o.f, o.size, from, o.size, out, 48);
  std::vector<uint32_t> expect;
  for (const uint32_t s : starts) {
    if (s >= from) expect.push_back(s);
  }
  if (expect.size() > 48) expect.erase(expect.begin(), expect.end() - 48);
  ASSERT_EQ(n, expect.size());
  for (uint8_t i = 0; i < n; ++i) EXPECT_EQ(out[i], expect[i]);
  EXPECT_EQ(chronicle::lineStartsIn(o.f, o.size, 0, o.size, out, 3), 3);
  EXPECT_EQ(out[2], starts.back());
}

TEST(ChronicleViewer, FindsHeadingsBothWays) {
  const std::string t = makeChronicle(80, {20, 21, 22, 50});
  Open o(t);
  const auto starts = lineStarts(t);
  for (size_t i = 0; i < starts.size(); i += 3) {
    const uint32_t p = starts[i];
    for (const int32_t age : {10, 16, 19, 20, 23, 49, 50, 51, 80, 81}) {
      uint32_t forward = chronicle::kNotFound;
      for (const uint32_t s : starts) {
        if (s >= p && headingAt(t, s) >= age) {
          forward = s;
          break;
        }
      }
      ASSERT_EQ(chronicle::findHeadingForward(o.f, o.size, p, age), forward) << "from " << p << " age " << age;
      uint32_t backward = chronicle::kNotFound;
      for (const uint32_t s : starts) {
        if (s < p && headingAt(t, s) >= 0 && headingAt(t, s) <= age) backward = s;
      }
      int32_t found = -1;
      ASSERT_EQ(chronicle::findHeadingBackward(o.f, o.size, p, age, &found), backward)
          << "before " << p << " age " << age;
      if (backward != chronicle::kNotFound) {
        EXPECT_EQ(found, headingAt(t, backward));
      }
    }
  }
}

TEST(ChronicleViewer, PagesAreTitledByTheirAge) {
  const std::string t = makeChronicle(40, {30});
  Open o(t);
  // The opening chapter has no age ("Chronicle · birth").
  EXPECT_EQ(chronicle::pageAge(o.f, o.size, 0), -1);
  EXPECT_EQ(chronicle::pageAge(o.f, o.size, static_cast<uint32_t>(t.find("Born"))), -1);
  // A page starting on a heading, or on the blank line just above it.
  const auto age25 = static_cast<uint32_t>(t.find("\nAge 25\n"));
  EXPECT_EQ(chronicle::pageAge(o.f, o.size, age25), 25);
  EXPECT_EQ(chronicle::pageAge(o.f, o.size, age25 + 1), 25);
  // A page starting on a season line.
  EXPECT_EQ(chronicle::pageAge(o.f, o.size, static_cast<uint32_t>(t.find("Autumn, age 33."))), 33);
  // A page starting on a line without an age: the heading above it.
  std::string odd = t;
  odd.insert(t.find("Summer, age 27."), "The rains came early that year.\n");
  Open o2(odd);
  EXPECT_EQ(chronicle::pageAge(o2.f, o2.size, static_cast<uint32_t>(odd.find("The rains"))), 27);
}

TEST(ChronicleViewer, HeadingsStayWithTheirEntriesWhenPagingBack) {
  const std::string t = makeChronicle(30);
  Open o(t);
  const auto spring = static_cast<uint32_t>(t.find("Spring, age 25."));
  const auto heading = static_cast<uint32_t>(t.find("Age 25\n"));
  EXPECT_EQ(chronicle::withHeadingAbove(o.f, o.size, spring), heading);
  const auto summer = static_cast<uint32_t>(t.find("Summer, age 25."));
  EXPECT_EQ(chronicle::withHeadingAbove(o.f, o.size, summer), summer);
  const auto born = static_cast<uint32_t>(t.find("Born"));
  EXPECT_EQ(chronicle::withHeadingAbove(o.f, o.size, born), static_cast<uint32_t>(t.find("Life 1")));
  EXPECT_EQ(chronicle::withHeadingAbove(o.f, o.size, 0), 0u);
}

TEST(ChronicleViewer, TenYearJumpsLandOnHeadingsAndClamp) {
  const std::string t = makeChronicle(70, {40, 41, 42, 43});
  Open o(t);
  const auto at = [&](const int age) {
    return static_cast<uint32_t>(t.find("\nAge " + std::to_string(age) + "\n") + 1);
  };
  const auto seasonOf = [&](const int age) {
    return static_cast<uint32_t>(t.find("Winter, age " + std::to_string(age)));
  };

  // Back from age 60 lands on Age 50; from 52 on the nearest at or below 42: Age 39.
  EXPECT_EQ(chronicle::jumpBack(o.f, o.size, seasonOf(60), 60), at(50));
  EXPECT_EQ(chronicle::jumpBack(o.f, o.size, seasonOf(52), 52), at(39));
  // Back past the start of the life: the beginning of the file.
  EXPECT_EQ(chronicle::jumpBack(o.f, o.size, seasonOf(22), 22), 0u);
  EXPECT_EQ(chronicle::jumpBack(o.f, o.size, 0, -1), 0u);

  // Forward from 25 lands on Age 35; from 32 on the first at or above 42: Age 44.
  EXPECT_EQ(chronicle::jumpForward(o.f, o.size, seasonOf(25), 25), at(35));
  EXPECT_EQ(chronicle::jumpForward(o.f, o.size, seasonOf(32), 32), at(44));
  // From the opening chapter: ten years after the starting age.
  EXPECT_EQ(chronicle::jumpForward(o.f, o.size, 0, -1), at(kStartAgeYears + 10));
  // Past the newest heading: "the newest page".
  EXPECT_EQ(chronicle::jumpForward(o.f, o.size, seasonOf(65), 65), chronicle::kNotFound);
  // A page that starts on its own heading jumps from that heading, not onto it.
  EXPECT_EQ(chronicle::jumpForward(o.f, o.size, at(50), 50), at(60));
}

TEST(ChronicleViewer, WorksOnAWholeLifeOf500KB) {
  // About the whole Mortal Plane: 16 to ~800, two lines a season.
  const std::string t = makeChronicle(560, {100, 101, 300}, 2);
  ASSERT_GT(t.size(), 500u * 1024u);
  Open o(t);
  const uint32_t lastHeading = static_cast<uint32_t>(t.rfind("\nAge ") + 1);
  EXPECT_EQ(chronicle::lineStartAtOrBefore(o.f, o.size, o.size - 1),
            static_cast<uint32_t>(t.rfind('\n', t.size() - 2) + 1));
  EXPECT_EQ(chronicle::findHeadingBackward(o.f, o.size, o.size, INT32_MAX), lastHeading);
  EXPECT_EQ(chronicle::pageAge(o.f, o.size, lastHeading), 560);
  EXPECT_EQ(chronicle::pageAge(o.f, o.size, o.size - 50), 560);

  // Ten-year steps from the newest page back to the beginning and forward again.
  uint32_t pos = lastHeading;
  int32_t age = 560;
  int steps = 0;
  while (pos != 0 && steps < 100) {
    pos = chronicle::jumpBack(o.f, o.size, pos, age);
    age = chronicle::pageAge(o.f, o.size, pos);
    ++steps;
  }
  EXPECT_EQ(pos, 0u);
  EXPECT_EQ(age, -1);
  EXPECT_GE(steps, 53);
  EXPECT_LE(steps, 56);
  steps = 0;
  while (pos != chronicle::kNotFound && steps < 100) {
    const int32_t before = age;
    pos = chronicle::jumpForward(o.f, o.size, pos, age);
    if (pos == chronicle::kNotFound) break;
    age = chronicle::pageAge(o.f, o.size, pos);
    EXPECT_GE(age, (before < 0 ? kStartAgeYears : before) + chronicle::kJumpYears);
    ++steps;
  }
  EXPECT_GE(steps, 53);
  // A gap in the ages (100, 101 missing): back from 110 stops at 99.
  EXPECT_EQ(chronicle::pageAge(o.f, o.size,
                               chronicle::jumpBack(o.f, o.size, static_cast<uint32_t>(t.find("Age 110\n")), 110)),
            99);
}

TEST(ChronicleViewer, EmptyAndHeadingLessFilesAreSafe) {
  {
    Open o("");
    EXPECT_EQ(chronicle::lineStartAtOrBefore(o.f, o.size, 0), 0u);
    EXPECT_EQ(chronicle::pageAge(o.f, o.size, 0), -1);
    EXPECT_EQ(chronicle::jumpBack(o.f, o.size, 0, -1), 0u);
    EXPECT_EQ(chronicle::jumpForward(o.f, o.size, 0, -1), chronicle::kNotFound);
    uint32_t out[4];
    EXPECT_EQ(chronicle::lineStartsIn(o.f, o.size, 0, 0, out, 4), 0);
  }
  {
    Open o("The Chronicle of Lin Yue\n\nLife 1: Lin Yue\n");
    EXPECT_EQ(chronicle::pageAge(o.f, o.size, 26), -1);
    EXPECT_EQ(chronicle::jumpForward(o.f, o.size, 0, -1), chronicle::kNotFound);
  }
}

// Stamina bank: 20 (user, session 9); still +1 per 15 minutes.
TEST(StaminaBank, HoldsTwentyAndStillFillsAtFourAnHour) {
  EXPECT_EQ(kStaminaCap, 20);
  CharacterDraft d;
  snprintf(d.name, sizeof(d.name), "Lin Yue");
  d.destiny.purity = 50;
  d.destiny.talent = 2;
  d.worldSeed = 0x5EED1234;
  GameState s;
  createCharacter(s, d, StatsSample{});
  s.progress.stamina = 0;
  VisitGains hour;
  hour.seconds = 60 * 60;
  hour.tiers.fullSeconds = 60 * 60;
  EXPECT_EQ(absorb(s, hour).staminaGained, 4);  // 1 per 15 minutes
  EXPECT_EQ(s.progress.stamina, 4);
  for (int i = 0; i < 9; ++i) absorb(s, hour);  // ten hours in all: 40 earned, banked 20, not 12
  EXPECT_EQ(s.progress.stamina, 20);
  // An old save at the old cap simply has room for 8 more.
  s.progress.stamina = 12;
  VisitGains twoHours = hour;
  twoHours.seconds *= 2;
  twoHours.tiers.fullSeconds *= 2;
  EXPECT_EQ(absorb(s, twoHours).staminaGained, 8);
  EXPECT_EQ(s.progress.stamina, 20);
}
