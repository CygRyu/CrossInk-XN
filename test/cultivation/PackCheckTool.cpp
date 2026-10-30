// cultivation_pack_check: checks content packs on a computer before they go
// on the SD card, using the same parser, checker and engine as the device.
//
//   cultivation_pack_check [pack.txt ...]                 check the built-in pack and these files
//   cultivation_pack_check --expand rumor [--count 10] [--seed 7] [--plane 1] [pack.txt ...]
//
// Files are layered on the built-in pack in the order given, as on the
// device. Exit status is 1 when any problem was found.
#include <BuiltinPack.h>
#include <Grammar.h>
#include <GrammarPack.h>
#include <PackCheck.h>
#include <Rng.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace cultivation;

namespace {

std::vector<std::string> g_names;

void printProblem(void*, const uint8_t source, const char* message) {
  const std::string& name = source < g_names.size() ? g_names[source] : g_names.front();
  std::printf("%s: %s\n", name.c_str(), message);
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  const char* expandKey = nullptr;
  int count = 10;
  uint32_t seed = 1;
  int plane = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--expand" && i + 1 < argc) {
      expandKey = argv[++i];
    } else if (a == "--count" && i + 1 < argc) {
      count = std::atoi(argv[++i]);
    } else if (a == "--seed" && i + 1 < argc) {
      seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (a == "--plane" && i + 1 < argc) {
      plane = std::atoi(argv[++i]);
    } else if (a == "--help" || a == "-h") {
      std::printf("usage: %s [--expand KEY [--count N] [--seed N] [--plane N]] [pack.txt ...]\n", argv[0]);
      return 0;
    } else {
      files.push_back(a);
    }
  }
  if (files.size() + 1 > MemoryPackReader::kMaxSources) {
    std::fprintf(stderr, "at most %d pack files at a time\n", MemoryPackReader::kMaxSources - 1);
    return 2;
  }

  std::vector<std::string> texts;
  texts.emplace_back(builtinPackText(), builtinPackSize());
  g_names.emplace_back("built-in pack");
  for (const auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "%s: cannot read\n", f.c_str());
      return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    texts.push_back(ss.str());
    g_names.push_back(f);
  }

  std::vector<PackSection> storage(1024);
  PackIndex index{storage.data(), 0, static_cast<uint16_t>(storage.size()), false};
  MemoryPackReader reader;
  uint32_t problems = 0;
  for (size_t i = 0; i < texts.size(); ++i) {
    PackParser parser(index, static_cast<uint8_t>(i));
    parsePackText(texts[i].data(), texts[i].size(), parser);
    reader.setSource(static_cast<uint8_t>(i), texts[i].data(), texts[i].size());
    if (parser.badHeaders() > 0) {
      std::printf("%s: %u bad [header] line(s), first on line %u (keys use a-z 0-9 . _ - @ ~)\n", g_names[i].c_str(),
                  parser.badHeaders(), parser.firstBadHeaderLine());
      problems += parser.badHeaders();
    }
    if (parser.linesOutsideSections() > 0) {
      std::printf("%s: %u line(s) outside any [section] are ignored\n", g_names[i].c_str(),
                  parser.linesOutsideSections());
      problems += parser.linesOutsideSections();
    }
  }
  index.applyReplacements();

  PackChecker checker(index, reader, PackCheckSink{&printProblem, nullptr});
  problems += checker.run(0);
  std::printf("%u section(s) in %zu pack(s), %u problem(s)\n", index.count, texts.size(), problems);

  if (expandKey != nullptr) {
    Grammar grammar(index, reader);
    GrammarContext ctx;
    ctx.plane = static_cast<uint8_t>(plane);
    const std::string text = std::string("{") + expandKey + "}";
    for (int n = 0; n < count; ++n) {
      grammar.beginScene();
      std::printf("- %s\n", grammar.expand(text.c_str(), contentSeed(seed, 50, static_cast<uint32_t>(n)), ctx));
    }
  }
  return problems > 0 ? 1 : 0;
}
