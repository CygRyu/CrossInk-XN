#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include <cstdio>

#include "QiNumber.h"

namespace cultivation {
namespace {

// Powers of ten that fit in uint64_t (10^0 .. 10^19). static const: flash, not DRAM.
static const uint64_t kPow10[] = {1ULL,
                                  10ULL,
                                  100ULL,
                                  1000ULL,
                                  10000ULL,
                                  100000ULL,
                                  1000000ULL,
                                  10000000ULL,
                                  100000000ULL,
                                  1000000000ULL,
                                  10000000000ULL,
                                  100000000000ULL,
                                  1000000000000ULL,
                                  10000000000000ULL,
                                  100000000000000ULL,
                                  1000000000000000ULL,
                                  10000000000000000ULL,
                                  100000000000000000ULL,
                                  1000000000000000000ULL,
                                  10000000000000000000ULL};
constexpr int kMaxPow10 = 19;
constexpr uint64_t kCanonicalLow = 100000000ULL;  // 10^8: lowest mantissa once exponent > 0

uint64_t shiftDown(uint64_t value, int digits) {
  if (digits <= 0) return value;
  if (digits > kMaxPow10) return 0;
  return value / kPow10[digits];
}

int digitCount(uint64_t v) {
  int d = 1;
  while (d <= kMaxPow10 && v >= kPow10[d]) ++d;
  return d;
}

// Brings a and b to a shared exponent while keeping as many digits as fit.
// Both mantissas are < 10^9, so scaling the larger-exponent one by up to 10^9
// stays below 10^18 and the sum of two stays below 2 * 10^18 < 2^64.
void align(const QiNumber& a, const QiNumber& b, uint64_t& outA, uint64_t& outB, int32_t& outExp) {
  const int32_t hi = a.exponent() > b.exponent() ? a.exponent() : b.exponent();
  const int32_t scale = hi < 9 ? hi : 9;
  outExp = hi - scale;
  auto bring = [&](const QiNumber& n) -> uint64_t {
    const int32_t delta = n.exponent() - outExp;
    if (delta >= 0) return static_cast<uint64_t>(n.mantissa()) * kPow10[delta];  // delta <= 9
    return shiftDown(n.mantissa(), -delta);
  };
  outA = bring(a);
  outB = bring(b);
}

}  // namespace

void QiNumber::normalize(uint64_t mantissa, int32_t exponent) {
  if (mantissa == 0) {
    mantissa_ = 0;
    exponent_ = 0;
    return;
  }
  while (mantissa >= kMantissaLimit) {
    mantissa /= 10;
    ++exponent;
  }
  while (exponent > 0 && mantissa < kCanonicalLow) {
    mantissa *= 10;
    --exponent;
  }
  if (exponent < 0) {  // only reachable from malformed input; floor toward zero
    mantissa = shiftDown(mantissa, -exponent);
    exponent = 0;
  }
  if (exponent > kMaxExponent) {  // saturate instead of wrapping
    mantissa = kMantissaLimit - 1;
    exponent = kMaxExponent;
  }
  mantissa_ = static_cast<uint32_t>(mantissa);
  exponent_ = static_cast<int16_t>(exponent);
}

QiNumber QiNumber::fromU64(const uint64_t value) {
  QiNumber n;
  n.normalize(value, 0);
  return n;
}

QiNumber QiNumber::fromParts(const uint32_t mantissa, const int16_t exponent) {
  QiNumber n;
  n.normalize(mantissa, exponent);
  return n;
}

bool QiNumber::toU64(uint64_t& out) const {
  if (exponent_ > kMaxPow10) {
    out = UINT64_MAX;
    return false;
  }
  const uint64_t scale = kPow10[exponent_];
  if (mantissa_ != 0 && scale > UINT64_MAX / mantissa_) {
    out = UINT64_MAX;
    return false;
  }
  out = static_cast<uint64_t>(mantissa_) * scale;
  return true;
}

QiNumber& QiNumber::add(const QiNumber& other) {
  uint64_t a = 0;
  uint64_t b = 0;
  int32_t e = 0;
  align(*this, other, a, b, e);
  normalize(a + b, e);
  return *this;
}

QiNumber& QiNumber::subtract(const QiNumber& other) {
  if (compare(other, *this) >= 0) {
    normalize(0, 0);
    return *this;
  }
  uint64_t a = 0;
  uint64_t b = 0;
  int32_t e = 0;
  align(*this, other, a, b, e);
  normalize(a > b ? a - b : 0, e);
  return *this;
}

QiNumber& QiNumber::mulRatio(const uint32_t numerator, const uint32_t denominator) {
  if (denominator == 0 || mantissa_ == 0) return *this;
  // mantissa < 10^9 and numerator < 2^32, so the product is < 4.3 * 10^18.
  uint64_t v = static_cast<uint64_t>(mantissa_) * numerator;
  int32_t e = exponent_;
  // Borrow precision from the exponent before dividing.
  while (e > 0 && v < kPow10[17]) {
    v *= 10;
    --e;
  }
  normalize(v / denominator, e);
  return *this;
}

int QiNumber::compare(const QiNumber& a, const QiNumber& b) {
  // Canonical form: exponent 0 holds values < 10^9; exponent > 0 holds a
  // mantissa in [10^8, 10^9). A larger exponent is therefore always larger.
  if (a.exponent_ != b.exponent_) return a.exponent_ < b.exponent_ ? -1 : 1;
  if (a.mantissa_ != b.mantissa_) return a.mantissa_ < b.mantissa_ ? -1 : 1;
  return 0;
}

uint16_t QiNumber::fillPermille(const QiNumber& part, const QiNumber& whole) {
  if (whole.isZero() || compare(part, whole) >= 0) return 1000;
  if (part.isZero()) return 0;
  const int32_t diff = whole.exponent_ - part.exponent_;  // >= 0 because part < whole
  if (diff > 3) return 0;                                 // below 1 permille
  const uint64_t w = static_cast<uint64_t>(whole.mantissa_) * kPow10[diff];
  const uint64_t permille = static_cast<uint64_t>(part.mantissa_) * 1000ULL / w;
  return static_cast<uint16_t>(permille > 1000 ? 1000 : permille);
}

size_t QiNumber::format(char* buf, const size_t len) const {
  if (buf == nullptr || len == 0) return 0;
  if (exponent_ == 0 && mantissa_ < 1000000u) {
    char digits[8];
    const int n = snprintf(digits, sizeof(digits), "%u", static_cast<unsigned>(mantissa_));
    size_t out = 0;
    for (int i = 0; i < n && out + 1 < len; ++i) {
      if (i > 0 && (n - i) % 3 == 0) {
        if (out + 2 >= len) break;
        buf[out++] = ',';
      }
      buf[out++] = digits[i];
    }
    buf[out] = '\0';
    return out;
  }
  // Three significant digits, rounded half up: d.dd e X
  const int digits = digitCount(mantissa_);
  int32_t sciExp = digits - 1 + exponent_;
  // Here mantissa >= 10^6 (exponent 0) or >= 10^8 (exponent > 0), so digits >= 7.
  uint64_t lead = shiftDown(mantissa_, digits - 3);
  const uint64_t rest = digits > 3 ? mantissa_ % kPow10[digits - 3] : 0;
  if (digits > 3 && rest * 2 >= kPow10[digits - 3]) ++lead;
  if (lead >= 1000) {  // 9.995 rounds to 10.0
    lead /= 10;
    ++sciExp;
  }
  const int n = snprintf(buf, len, "%u.%02ue%d", static_cast<unsigned>(lead / 100), static_cast<unsigned>(lead % 100),
                         static_cast<int>(sciExp));
  if (n < 0) {
    buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n) < len ? static_cast<size_t>(n) : len - 1;
}

void QiNumber::serialize(uint8_t out[kSerializedSize]) const {
  out[0] = static_cast<uint8_t>(mantissa_);
  out[1] = static_cast<uint8_t>(mantissa_ >> 8);
  out[2] = static_cast<uint8_t>(mantissa_ >> 16);
  out[3] = static_cast<uint8_t>(mantissa_ >> 24);
  const uint16_t e = static_cast<uint16_t>(exponent_);
  out[4] = static_cast<uint8_t>(e);
  out[5] = static_cast<uint8_t>(e >> 8);
}

QiNumber QiNumber::deserialize(const uint8_t in[kSerializedSize]) {
  const uint32_t m = static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
                     (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
  const int16_t e = static_cast<int16_t>(static_cast<uint16_t>(in[4] | (in[5] << 8)));
  QiNumber n;
  n.normalize(m, e);  // repairs anything non-canonical read from disk
  return n;
}

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
