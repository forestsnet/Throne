#include "include/ui/fsnt/FsntPalette.hpp"

#include "include/ui/setting/ThemeManager.hpp"

namespace Fsnt {
    Palette PaletteFor(const bool dark) {
        Palette p;
        p.dark = dark;
        if (dark) {
            p.bg = QColor("#0C0E13");
            p.surface = QColor("#12151C");
            p.card = QColor("#181C25");
            p.cardHover = QColor("#212734");
            p.border = QColor("#242B39");
            p.text = QColor("#EEF1F6");
            p.textMuted = QColor("#7E8799");
            p.accent = QColor("#5B7CFA");
            p.accentSoft = QColor(91, 124, 250, 38);
            p.success = QColor("#2FD27C");
            p.danger = QColor("#FF5A65");
            p.warn = QColor("#FFB020");
        } else {
            p.bg = QColor("#F4F6F9");
            p.surface = QColor("#FFFFFF");
            p.card = QColor("#FFFFFF");
            p.cardHover = QColor("#EEF1F6");
            p.border = QColor("#D6DBE4");
            p.text = QColor("#141821");
            p.textMuted = QColor("#69728A");
            p.accent = QColor("#3B63E8");
            p.accentSoft = QColor(59, 99, 232, 30);
            p.success = QColor("#0FA45C");
            p.danger = QColor("#DC3B48");
            p.warn = QColor("#B87400");
        }
        return p;
    }

    Palette CurrentPalette() {
        // lightnessF, а не value: value у насыщенного тёмно-синего обманчиво высок.
        return PaletteFor(themeManager()->tokens.surface.lightnessF() < 0.5);
    }
} // namespace Fsnt
