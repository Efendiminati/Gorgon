#pragma once

#include <Gorgon/Widgets/Generator.h>
#include <memory>

namespace Gorgon :: Editor {

    /// Creates and activates a dark-themed Gorgon SimpleGenerator suitable for the 2D Editor.
    /// The returned generator must outlive all widgets created with it.
    std::unique_ptr<Gorgon::Widgets::SimpleGenerator> CreateDarkEditorTheme(float density = 9.5f);

} // namespace Gorgon::Editor
