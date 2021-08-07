#pragma once

#include <QString>

[[noreturn]] void FatalError(const QString &content, bool report);
