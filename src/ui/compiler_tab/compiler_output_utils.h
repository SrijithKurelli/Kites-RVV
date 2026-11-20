#pragma once

#include <QRegularExpression>
#include <QString>

namespace Kites::CompilerOutput
{

// Compiler Explorer may include ANSI control sequences in diagnostics and assembly.
// The GUI renders plain text, so remove CSI sequences before displaying the output.
inline QString stripAnsiEscapeSequences(const QString &text)
{
    static const QRegularExpression ansiControlSequence(
        QStringLiteral("\\x1B\\[[0-?]*[ -/]*[@-~]"));

    QString plainText = text;
    plainText.remove(ansiControlSequence);
    return plainText;
}

} // namespace Kites::CompilerOutput
