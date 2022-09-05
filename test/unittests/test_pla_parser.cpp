#include "core/io/pla_parser.hpp"

#include "gtest/gtest.h"

using namespace syrec;

class PlaParserTest: public testing::Test {
protected:
    std::string test_circuits_dir = "./circuits/";
    TruthTable  testPla;

    TruthTable::Cube::Value emptyVal;

    TruthTable::Cube c11{};

    TruthTable::Cube c1{};

    TruthTable::Cube c1_{};
    TruthTable::Cube c_1{};

    void SetUp() override {
        c11 = TruthTable::Cube::fromInteger(0b11U, 2U);

        c1 = TruthTable::Cube::fromInteger(0b1U, 1U);

        c1_.emplace_back(true);
        c1_.emplace_back(emptyVal);

        c_1.emplace_back(emptyVal);
        c_1.emplace_back(true);
    }
};

TEST_F(PlaParserTest, andTest) {
    std::string circAnd = test_circuits_dir + "and.pla";

    EXPECT_TRUE(read_pla(testPla, circAnd));

    EXPECT_EQ(testPla.nInputs(), 2U);
    EXPECT_EQ(testPla.nOutputs(), 1U);
    EXPECT_EQ(testPla.size(), 1U);

    auto it11 = testPla.find(0b11U, 2U);

    EXPECT_TRUE(it11 != testPla.end());
    EXPECT_TRUE(it11->second.equals(0b1U, 1U));
}

TEST_F(PlaParserTest, orTest) {
    std::string circOr = test_circuits_dir + "or.pla";

    EXPECT_TRUE(read_pla(testPla, circOr));

    EXPECT_EQ(testPla.nInputs(), 2U);
    EXPECT_EQ(testPla.nOutputs(), 1U);
    EXPECT_EQ(testPla.size(), 2U);

    auto it_1 = testPla.find("-1");

    EXPECT_TRUE(it_1 != testPla.end());
    EXPECT_TRUE(it_1->second.equals(0b1U, 1U));

    auto it1_ = testPla.find("1-");

    EXPECT_TRUE(it1_ != testPla.end());
    EXPECT_TRUE(it1_->second.equals(0b1U, 1U));
}
