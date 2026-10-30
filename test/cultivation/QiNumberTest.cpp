#include <QiNumber.h>
#include <gtest/gtest.h>

#include <string>

using cultivation::QiNumber;

namespace {
std::string fmt(const QiNumber& n) {
  char buf[32];
  n.format(buf, sizeof(buf));
  return buf;
}
uint64_t u64(const QiNumber& n) {
  uint64_t v = 0;
  EXPECT_TRUE(n.toU64(v));
  return v;
}
}  // namespace

TEST(QiNumber, IsSixBytesOnDisk) { EXPECT_EQ(QiNumber::kSerializedSize, 6u); }

TEST(QiNumber, SmallValuesAreExact) {
  const QiNumber n = QiNumber::fromU64(6184753);  // realm 9 total Qi
  EXPECT_EQ(n.exponent(), 0);
  EXPECT_EQ(u64(n), 6184753u);
  EXPECT_EQ(u64(QiNumber::fromU64(999999999)), 999999999u);
}

TEST(QiNumber, LargeValuesKeepNineDigits) {
  const QiNumber n = QiNumber::fromU64(123456789012345ULL);
  EXPECT_EQ(n.mantissa(), 123456789u);
  EXPECT_EQ(n.exponent(), 6);
  EXPECT_EQ(u64(n), 123456789000000ULL);
}

TEST(QiNumber, AddCarriesAcrossTheExactLimit) {
  QiNumber a = QiNumber::fromU64(999999999);
  a.add(QiNumber::fromU64(1));
  EXPECT_EQ(u64(a), 1000000000ULL);
  EXPECT_EQ(a.exponent(), 1);

  QiNumber b = QiNumber::fromU64(500);
  b.add(QiNumber::fromU64(250));
  EXPECT_EQ(u64(b), 750u);
}

TEST(QiNumber, AddingTinyToHugeIsHarmless) {
  QiNumber big = QiNumber::fromParts(427000000, 7);  // 4.27e15
  const QiNumber before = big;
  big.add(QiNumber::fromU64(5));
  EXPECT_EQ(big, before);
}

TEST(QiNumber, SubtractSaturatesAtZero) {
  QiNumber a = QiNumber::fromU64(100);
  a.subtract(QiNumber::fromU64(250));
  EXPECT_TRUE(a.isZero());

  QiNumber b = QiNumber::fromU64(1000000000000ULL);
  b.subtract(QiNumber::fromU64(1));
  EXPECT_EQ(u64(b), 999999999000ULL);  // 9 significant digits
  QiNumber c = QiNumber::fromU64(1000);
  c.subtract(QiNumber::fromU64(1));
  EXPECT_EQ(u64(c), 999u);
}

TEST(QiNumber, MulRatioAppliesPermilleMultipliers) {
  QiNumber a = QiNumber::fromU64(1000);
  a.mulRatio(600, 1000);  // Choked Meridians x0.6
  EXPECT_EQ(u64(a), 600u);

  QiNumber b = QiNumber::fromParts(100000000, 10);  // 1e18
  b.mulRatio(1500, 1000);
  EXPECT_EQ(b.mantissa(), 150000000u);
  EXPECT_EQ(b.exponent(), 10);

  QiNumber c = QiNumber::fromU64(7);
  c.mulRatio(1, 0);  // no-op on a zero denominator
  EXPECT_EQ(u64(c), 7u);
}

TEST(QiNumber, CompareOrdersAcrossExponents) {
  const QiNumber a = QiNumber::fromU64(999999999);
  const QiNumber b = QiNumber::fromU64(1000000000);
  const QiNumber c = QiNumber::fromParts(100000000, 2);  // 1e10
  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
  EXPECT_GT(c, a);
  EXPECT_EQ(QiNumber::fromU64(0), QiNumber());
  EXPECT_LT(QiNumber(), QiNumber::fromU64(1));
}

TEST(QiNumber, FromPartsNormalizes) {
  // 10 * 10^8 written un-normalized equals 1e9.
  EXPECT_EQ(QiNumber::fromParts(10, 8), QiNumber::fromU64(1000000000));
  EXPECT_EQ(QiNumber::fromParts(5, 0), QiNumber::fromU64(5));
}

TEST(QiNumber, FillPermille) {
  EXPECT_EQ(QiNumber::fillPermille(QiNumber::fromU64(250), QiNumber::fromU64(1000)), 250);
  EXPECT_EQ(QiNumber::fillPermille(QiNumber::fromU64(1500), QiNumber::fromU64(1000)), 1000);
  EXPECT_EQ(QiNumber::fillPermille(QiNumber(), QiNumber::fromU64(1000)), 0);
  EXPECT_EQ(QiNumber::fillPermille(QiNumber::fromU64(1), QiNumber()), 1000);
  EXPECT_EQ(QiNumber::fillPermille(QiNumber::fromU64(500000000), QiNumber::fromU64(2000000000)), 250);
  EXPECT_EQ(QiNumber::fillPermille(QiNumber::fromU64(5), QiNumber::fromParts(100000000, 9)), 0);
}

TEST(QiNumber, Format) {
  EXPECT_EQ(fmt(QiNumber()), "0");
  EXPECT_EQ(fmt(QiNumber::fromU64(950)), "950");
  EXPECT_EQ(fmt(QiNumber::fromU64(12345)), "12,345");
  EXPECT_EQ(fmt(QiNumber::fromU64(999999)), "999,999");
  EXPECT_EQ(fmt(QiNumber::fromU64(6184753)), "6.18e6");
  EXPECT_EQ(fmt(QiNumber::fromU64(4270000000000000ULL)), "4.27e15");
  EXPECT_EQ(fmt(QiNumber::fromU64(9995000)), "1.00e7");  // rounds up a digit
  EXPECT_EQ(fmt(QiNumber::fromParts(123000000, 9000)), "1.23e9008");
}

TEST(QiNumber, FormatNeverOverflowsTheBuffer) {
  char buf[4];
  QiNumber::fromU64(12345).format(buf, sizeof(buf));
  EXPECT_EQ(std::string(buf), "12");  // truncated before a dangling comma
  char one[1];
  QiNumber::fromU64(7).format(one, sizeof(one));
  EXPECT_EQ(one[0], '\0');
}

TEST(QiNumber, SerializeRoundTrip) {
  for (const QiNumber& n :
       {QiNumber(), QiNumber::fromU64(42), QiNumber::fromU64(999999999), QiNumber::fromParts(427000000, 7),
        QiNumber::fromParts(999999999, QiNumber::kMaxExponent)}) {
    uint8_t buf[QiNumber::kSerializedSize];
    n.serialize(buf);
    EXPECT_EQ(QiNumber::deserialize(buf), n);
  }
}

TEST(QiNumber, ExponentSaturatesInsteadOfWrapping) {
  QiNumber n = QiNumber::fromParts(999999999, QiNumber::kMaxExponent);
  n.add(n);
  EXPECT_EQ(n.exponent(), QiNumber::kMaxExponent);
  EXPECT_EQ(n.mantissa(), 999999999u);
  uint64_t out = 0;
  EXPECT_FALSE(n.toU64(out));
}
