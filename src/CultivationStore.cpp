#include "CultivationStore.h"

void CultivationStore::toJson(JsonDocument& doc) const {
  doc["v"] = kSaveVersion;
  doc["seed"] = seed_;
  doc["path"] = static_cast<uint8_t>(path_);
  doc["banked"] = ledger_.banked;
  doc["lastDerived"] = ledger_.lastDerived;
  doc["seenQi"] = seenQi_;
  doc["jFrom"] = journalFrom_;
  doc["jTo"] = journalTo_;
}

bool CultivationStore::fromJson(JsonVariantConst doc) {
  // Unknown future versions are still read field-by-field; missing fields keep defaults.
  seed_ = doc["seed"] | 0u;
  const uint8_t rawPath = doc["path"] | 0u;
  path_ = rawPath <= static_cast<uint8_t>(Cultivation::Path::Rogue) ? static_cast<Cultivation::Path>(rawPath)
                                                                    : Cultivation::Path::Undecided;
  ledger_.banked = doc["banked"] | 0u;
  ledger_.lastDerived = doc["lastDerived"] | 0u;
  seenQi_ = doc["seenQi"] | 0u;
  journalFrom_ = doc["jFrom"] | 0u;
  journalTo_ = doc["jTo"] | 0u;
  // Keep the journal window sane if the file was edited by hand.
  if (journalFrom_ > journalTo_) journalFrom_ = journalTo_;
  if (seenQi_ > ledger_.banked) seenQi_ = ledger_.banked;
  return true;
}
