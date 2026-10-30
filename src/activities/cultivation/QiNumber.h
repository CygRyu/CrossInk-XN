#pragma once

#include <cstddef>
#include <cstdint>

namespace cultivation {

// Qi and every cost compared against it. Late-game Qi outgrows 64-bit
// integers, so the value is stored as mantissa * 10^exponent.
//
// - 6 bytes on disk (uint32 mantissa + int16 exponent), 8 in RAM.
// - Integer-only math: the ESP32-C3 has no FPU, so no float/double anywhere.
// - Values stay exact while they fit below 10^9 (exponent 0). Above that the
//   mantissa keeps 9 significant digits and the exponent grows. The whole
//   Mortal Plane needs at most ~6.2 million Qi per realm, so it is exact.
// - Never negative: subtraction saturates at zero.
class QiNumber {
 public:
  static constexpr uint32_t kMantissaLimit = 1000000000u;  // 10^9, exclusive
  static constexpr int16_t kMaxExponent = 9999;

  constexpr QiNumber() = default;
  static QiNumber fromU64(uint64_t value);
  static QiNumber fromParts(uint32_t mantissa, int16_t exponent);  // normalizes

  uint32_t mantissa() const { return mantissa_; }
  int16_t exponent() const { return exponent_; }
  bool isZero() const { return mantissa_ == 0; }

  // Returns false when the value does not fit (exponent too large); out is
  // then UINT64_MAX.
  bool toU64(uint64_t& out) const;

  QiNumber& add(const QiNumber& other);
  // Saturates at zero when other > this.
  QiNumber& subtract(const QiNumber& other);
  // this * numerator / denominator, with a 64-bit intermediate. Used for
  // multipliers in permille (numerator 1250, denominator 1000 = x1.25).
  // denominator 0 leaves the value unchanged.
  QiNumber& mulRatio(uint32_t numerator, uint32_t denominator);

  // -1, 0, 1
  static int compare(const QiNumber& a, const QiNumber& b);
  bool operator==(const QiNumber& o) const { return compare(*this, o) == 0; }
  bool operator!=(const QiNumber& o) const { return compare(*this, o) != 0; }
  bool operator<(const QiNumber& o) const { return compare(*this, o) < 0; }
  bool operator<=(const QiNumber& o) const { return compare(*this, o) <= 0; }
  bool operator>(const QiNumber& o) const { return compare(*this, o) > 0; }
  bool operator>=(const QiNumber& o) const { return compare(*this, o) >= 0; }

  // part / whole in permille (0..1000), clamped. whole == 0 gives 1000.
  static uint16_t fillPermille(const QiNumber& part, const QiNumber& whole);

  // Language-neutral display: "12,345" below one million, then three
  // significant digits in scientific form: "4.27e15". Named units ("4.27
  // quadrillion") belong to the UI layer, which can translate them.
  // Always NUL-terminates when len > 0. Returns the written length.
  size_t format(char* buf, size_t len) const;

  // 6-byte little-endian serialization.
  static constexpr size_t kSerializedSize = 6;
  void serialize(uint8_t out[kSerializedSize]) const;
  static QiNumber deserialize(const uint8_t in[kSerializedSize]);

 private:
  constexpr QiNumber(uint32_t m, int16_t e) : mantissa_(m), exponent_(e) {}
  void normalize(uint64_t mantissa, int32_t exponent);

  uint32_t mantissa_ = 0;
  int16_t exponent_ = 0;
};

}  // namespace cultivation
