#pragma once
#include "include/global/Configs.hpp"
inline std::function<void(QString)> MW_handle_deeplink;
inline QString TrimInvisible(const QString &text) { return text.trimmed(); }
inline QString CleanPastedLink(const QString &text) { return text.trimmed(); }
