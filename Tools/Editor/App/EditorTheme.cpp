#include "EditorTheme.h"

#include <Gorgon/Graphics/Color.h>
#include <Gorgon/UI.h>

namespace Gorgon :: Editor {

    std::unique_ptr<Gorgon::Widgets::SimpleGenerator> CreateDarkEditorTheme(float density) {
        using namespace Gorgon::Graphics;
        using Triplet = Color::Triplet<RGBA>;

        // Dark editor palette (RGBA constructor: r, g, b, a)
        const RGBA textPrimary   (228, 232, 240, 255);
        const RGBA textMuted     (154, 162, 178, 255);
        const RGBA textDisabled  (105, 112, 126, 255);
        const RGBA bgWorkspace   (22,  24,  30,  255);
        const RGBA bgPanel       (32,  35,  43,  255);
        const RGBA bgControl     (44,  48,  58,  255);
        const RGBA bgControlAlt  (38,  42,  52,  255);
        const RGBA bgEdit        (24,  26,  33,  255);
        const RGBA bgHover       (58,  64,  78,  255);
        const RGBA bgDown        (70,  78,  96,  255);
        const RGBA accent        (59,  130, 246, 255);
        const RGBA accentDark    (37,  99,  235, 255);
        const RGBA borderSubtle  (64,  70,  84,  255);
        const RGBA borderFocus   (96,  165, 250, 255);

        Color::TripletPack pack;
        pack.Set(Color::Regular,          Triplet(textPrimary,  bgControl,    borderSubtle));
        pack.Set(Color::Alternate,        Triplet(textPrimary,  bgControlAlt, borderSubtle));
        pack.Set(Color::Title,            Triplet(textPrimary,  bgPanel,      borderSubtle));
        pack.Set(Color::Emphasis,         Triplet(borderFocus,  bgControl,    borderFocus));
        pack.Set(Color::Inverted,         Triplet(bgWorkspace,  textPrimary,  borderSubtle));
        pack.Set(Color::Selection,        Triplet(textPrimary,  accent,       borderFocus));
        pack.Set(Color::Disabled,         Triplet(textDisabled, bgPanel,      borderSubtle));
        pack.Set(Color::Highlight,        Triplet(textPrimary,  accentDark,   borderFocus));
        pack.Set(Color::Edit,             Triplet(textPrimary,  bgEdit,       borderSubtle));
        pack.Set(Color::Readonly,         Triplet(textMuted,    bgPanel,      borderSubtle));
        pack.Set(Color::Separator,        Triplet(borderSubtle, bgPanel,      borderSubtle));
        pack.Set(Color::Container,        Triplet(textPrimary,  bgPanel,      borderSubtle));
        pack.Set(Color::ActiveContainer,  Triplet(textPrimary,  bgPanel,      borderFocus));
        pack.Set(Color::PassiveContiner,  Triplet(textMuted,    bgPanel,      borderSubtle));
        pack.Set(Color::Info,             Triplet(textMuted,    bgPanel,      borderSubtle));
        pack.Set(Color::Message,          Triplet(textPrimary,  bgControl,    borderSubtle));
        pack.Set(Color::Warning,          Triplet(RGBA(251, 191, 36, 255), bgControl, RGBA(217, 119, 6, 255)));
        pack.Set(Color::Error,            Triplet(RGBA(248, 113, 113, 255), bgControl, RGBA(220, 38, 38, 255)));
        pack.Set(Color::Success,          Triplet(RGBA(74, 222, 128, 255), bgControl, RGBA(22, 163, 74, 255)));
        pack.Set(Color::Link,             Triplet(borderFocus,  bgPanel,      borderFocus));
        pack.Set(Color::Hover,            Triplet(textPrimary,  bgHover,      borderFocus));
        pack.Set(Color::Down,             Triplet(textPrimary,  bgDown,       accent));
        pack.Set(Color::Active,           Triplet(textPrimary,  accent,       borderFocus));
        pack.Set(Color::Focus,            Triplet(borderFocus,  bgControl,    borderFocus));
        pack.Set(Color::Groove,           Triplet(borderSubtle, bgWorkspace,  borderSubtle));
        pack.Set(Color::HighContrast,     Triplet(RGBA(255, 255, 255, 255), bgWorkspace, borderFocus));
        pack.Set(Color::Object,           Triplet(textPrimary,  bgControl,    borderSubtle));
        pack.Set(Color::Odd,              Triplet(textPrimary,  bgPanel,      borderSubtle));
        pack.Set(Color::Even,             Triplet(textPrimary,  bgControlAlt, borderSubtle));
        pack.Set(Color::Workspace,        Triplet(textPrimary,  bgWorkspace,  borderSubtle));

        auto gen = std::make_unique<Gorgon::Widgets::SimpleGenerator>();
        gen->Init(
            Gorgon::UI::FontHeight(density, 10),
            std::move(pack),
            "",
            "",
            density,
            1.0f,
            Gorgon::Widgets::SimpleGenerator::LessRounded
        );
        Gorgon::UI::Initialize(*gen);
        return gen;
    }

} // namespace Gorgon::Editor
