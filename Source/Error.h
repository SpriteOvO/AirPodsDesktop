#pragma once

#include <string>

[[noreturn]] void FatalError(const std::string &content, bool report);
