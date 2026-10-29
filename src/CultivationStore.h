#pragma once
#include <ArduinoJson.h>
#include <CultivationEngine.h>
#include <PersistableStore.h>

#include <cstdint>

/**
 * Tiny persisted save for the reading-driven cultivation game
 * (/.crosspoint/cultivation.json). Only things that cannot be derived from
 * reading stats live here: a random seed, the chosen Dao path, and the Qi
 * ledger that keeps the character safe from reading-stats resets.
 *
 * Optional heavier layers (techniques, sect, inventory) should add their own
 * fields behind a version bump; see docs/cultivation.md.
 */
class CultivationStore : public PersistableStore<CultivationStore> {
 private:
  CultivationStore() = default;
  friend class PersistableStore<CultivationStore>;

  static constexpr uint32_t kSaveVersion = 1;

  uint32_t seed_ = 0;
  Cultivation::Path path_ = Cultivation::Path::Undecided;
  Cultivation::Ledger ledger_;
  uint32_t seenQi_ = 0;      // banked Qi when the journal was last advanced
  uint32_t journalFrom_ = 0;  // journal window shown on the Journal page
  uint32_t journalTo_ = 0;

 public:
  static const char* getFilePath() { return "/.crosspoint/cultivation.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  uint32_t seed() const { return seed_; }
  void setSeed(const uint32_t seed) { seed_ = seed; }
  Cultivation::Path path() const { return path_; }
  void setPath(const Cultivation::Path path) { path_ = path; }
  const Cultivation::Ledger& ledger() const { return ledger_; }
  void setLedger(const Cultivation::Ledger& ledger) { ledger_ = ledger; }
  uint32_t seenQi() const { return seenQi_; }
  uint32_t journalFrom() const { return journalFrom_; }
  uint32_t journalTo() const { return journalTo_; }
  // Records that the journal now covers (from, to] and the player has seen up to `to`.
  void advanceJournal(const uint32_t from, const uint32_t to) {
    journalFrom_ = from;
    journalTo_ = to;
    seenQi_ = to;
  }
};

#define CULTIVATION CultivationStore::getInstance()
