#pragma once

#include <QLoggingCategory>

// Logging categories. Enable/disable via QT_LOGGING_RULES=banks.viz=true etc.
// Default: all on.
Q_DECLARE_LOGGING_CATEGORY(logViz)
Q_DECLARE_LOGGING_CATEGORY(logAudio)
Q_DECLARE_LOGGING_CATEGORY(logOverlay)
Q_DECLARE_LOGGING_CATEGORY(logFav)
Q_DECLARE_LOGGING_CATEGORY(logSpectrum)
