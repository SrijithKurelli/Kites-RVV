#include <gtest/gtest.h>

#include "processor/registers.h"
#include "ui/common/display_base_types.h"
#include "ui/register_table/registermodel.h"

#include <array>
#include <cstdint>

using namespace Kites;

namespace
{
std::array<uint8_t, 16> packLanes32(const std::array<uint32_t, 4> &lanes)
{
    std::array<uint8_t, 16> bytes{};
    for (size_t lane = 0; lane < lanes.size(); ++lane)
        for (size_t byte = 0; byte < 4; ++byte)
            bytes[lane * 4 + byte] = static_cast<uint8_t>((lanes[lane] >> (8 * byte)) & 0xffu);
    return bytes;
}
} // namespace

TEST(RegisterModelTest, VectorLanesRenderAsSignedDecimalElements)
{
    RegisterFile registers;
    registers.WriteVector(0, packLanes32({3u, 11u, 0xffffffffu, 0u}));
    RegisterModel model(nullptr, &registers);
    model.setVectorElementWidth(32);
    model.setDisplayBase(Base::Decimal);

    EXPECT_EQ(model.data(model.index(64, 2)).toString(), QString("[3, 11, -1, 0]"));
}

TEST(RegisterModelTest, VectorLanesRenderAsZeroPaddedHexadecimalElements)
{
    RegisterFile registers;
    registers.WriteVector(0, packLanes32({3u, 11u, 0xffffffffu, 0u}));
    RegisterModel model(nullptr, &registers);
    model.setVectorElementWidth(32);
    model.setDisplayBase(Base::Hexadecimal);

    EXPECT_EQ(model.data(model.index(64, 2)).toString(),
              QString("[0x00000003, 0x0000000B, 0xFFFFFFFF, 0x00000000]"));
}

TEST(RegisterModelTest, VectorLanesRenderAsGroupedBinaryElements)
{
    RegisterFile registers;
    registers.WriteVector(0, packLanes32({3u, 11u, 0u, 0u}));
    RegisterModel model(nullptr, &registers);
    model.setVectorElementWidth(32);
    model.setDisplayBase(Base::Binary);

    const QString value = model.data(model.index(64, 2)).toString();
    EXPECT_TRUE(value.startsWith("[0b00000000 00000000 00000000 00000011, "
                                 "0b00000000 00000000 00000000 00001011"));
    EXPECT_TRUE(value.endsWith("]"));
}

TEST(RegisterModelTest, VectorElementWidthControlsLaneCountAndInterpretation)
{
    RegisterFile registers;
    std::array<uint8_t, 16> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(i + 1);
    registers.WriteVector(0, bytes);

    RegisterModel model(nullptr, &registers);
    model.setDisplayBase(Base::Decimal);
    model.setVectorElementWidth(8);
    EXPECT_EQ(model.data(model.index(64, 2)).toString(),
              QString("[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]"));

    model.setVectorElementWidth(64);
    EXPECT_EQ(model.data(model.index(64, 2)).toString(),
              QString("[578437695752307201, 1157159078456920585]"));
}
