#include <gtest/gtest.h>

#include "ui/compiler_tab/compiler_output_utils.h"

TEST(CompilerOutputTest, RemovesAnsiColorAndEraseLineSequences)
{
    const QString formatted = QStringLiteral("\x1B[01merror: invalid option\x1B[K");
    EXPECT_EQ(Kites::CompilerOutput::stripAnsiEscapeSequences(formatted),
              QStringLiteral("error: invalid option"));
}

TEST(CompilerOutputTest, LeavesPlainTextUnchanged)
{
    const QString plainText = QStringLiteral("warning: unused variable");
    EXPECT_EQ(Kites::CompilerOutput::stripAnsiEscapeSequences(plainText), plainText);
}
