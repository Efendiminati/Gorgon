#pragma once

#include <Gorgon/Input/Keyboard.h>
#include <Gorgon/Input/Mouse.h>

#include <cctype>
#include <set>
#include <string>

namespace Gorgon :: Editor {

    /**
     * Canonical spelling of a key or mouse button name: trimmed, lower case, with the common
     * aliases folded onto one form ("Return" -> "enter", "LMB" -> "left", "ArrowLeft" -> "left").
     *
     * Scripts pass names as strings, so the editor and the runtime both normalize before storing
     * or looking anything up. That way "W", "w" and " W " all hit the same entry.
     */
    inline std::string NormalizeInputName(const std::string &name) {
        std::string out;
        out.reserve(name.size());

        for(char c : name) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if(std::isspace(uc) || c == '_' || c == '-')
                continue;

            out.push_back(static_cast<char>(std::tolower(uc)));
        }

        if(out.empty())
            return out;

        // Keys
        if(out == "return") return "enter";
        if(out == "esc") return "escape";
        if(out == "spacebar") return "space";
        if(out == "ctrl") return "control";
        if(out == "del") return "delete";
        if(out == "arrowleft") return "left";
        if(out == "arrowright") return "right";
        if(out == "arrowup") return "up";
        if(out == "arrowdown") return "down";
        if(out == "pgup") return "pageup";
        if(out == "pgdn" || out == "pgdown") return "pagedown";

        // Mouse buttons
        if(out == "lmb" || out == "primary") return "left";
        if(out == "rmb" || out == "secondary") return "right";
        if(out == "mmb" || out == "wheel") return "middle";

        return out;
    }

    /// Script name of a Gorgon keycode: "w", "left", "space", "f1", ... Empty for keycodes the
    /// input API does not expose, which callers are expected to skip.
    inline std::string KeyNameFromCode(Gorgon::Input::Key key) {
        namespace K = Gorgon::Input::Keyboard::Keycodes;

        switch(key) {
        case K::Left:        return "left";
        case K::Right:       return "right";
        case K::Up:          return "up";
        case K::Down:        return "down";
        case K::Space:       return "space";
        case K::Enter:       return "enter";
        case K::Numpad_Enter:return "enter";
        case K::Escape:      return "escape";
        case K::Tab:         return "tab";
        case K::Backspace:   return "backspace";
        case K::Delete:      return "delete";
        case K::Insert:      return "insert";
        case K::Home:        return "home";
        case K::End:         return "end";
        case K::PageUp:      return "pageup";
        case K::PageDown:    return "pagedown";
        case K::Shift:       return "shift";
        case K::RShift:      return "shift";
        case K::Control:     return "control";
        case K::RControl:    return "control";
        case K::Alt:         return "alt";
        case K::RAlt:        return "alt";
        case K::Meta:        return "meta";
        case K::RMeta:       return "meta";
        case K::F1:          return "f1";
        case K::F2:          return "f2";
        case K::F3:          return "f3";
        case K::F4:          return "f4";
        case K::F5:          return "f5";
        case K::F6:          return "f6";
        case K::F7:          return "f7";
        case K::F8:          return "f8";
        case K::F9:          return "f9";
        case K::F10:         return "f10";
        case K::F11:         return "f11";
        case K::F12:         return "f12";
        case K::Number_0:    return "0";
        case K::Number_1:    return "1";
        case K::Number_2:    return "2";
        case K::Number_3:    return "3";
        case K::Number_4:    return "4";
        case K::Number_5:    return "5";
        case K::Number_6:    return "6";
        case K::Number_7:    return "7";
        case K::Number_8:    return "8";
        case K::Number_9:    return "9";
        case K::A:           return "a";
        case K::B:           return "b";
        case K::C:           return "c";
        case K::D:           return "d";
        case K::E:           return "e";
        case K::F:           return "f";
        case K::G:           return "g";
        case K::H:           return "h";
        case K::I:           return "i";
        case K::J:           return "j";
        case K::K:           return "k";
        case K::L:           return "l";
        case K::M:           return "m";
        case K::N:           return "n";
        case K::O:           return "o";
        case K::P:           return "p";
        case K::Q:           return "q";
        case K::R:           return "r";
        case K::S:           return "s";
        case K::T:           return "t";
        case K::U:           return "u";
        case K::V:           return "v";
        case K::W:           return "w";
        case K::X:           return "x";
        case K::Y:           return "y";
        case K::Z:           return "z";
        default:             break;
        }

        return {};
    }

    /// Script name of a Gorgon mouse button: "left", "right", "middle". Empty for anything else.
    /// Gorgon's button constants set bit 9, so only the low button bits are compared.
    inline std::string MouseButtonName(Gorgon::Input::Mouse::Button button) {
        using Btn = Gorgon::Input::Mouse::Button;
        const int bits = static_cast<int>(button) & 0x1FF;

        if(bits & static_cast<int>(Btn::Left))   return "left";
        if(bits & static_cast<int>(Btn::Right))  return "right";
        if(bits & static_cast<int>(Btn::Middle)) return "middle";

        return {};
    }

    /**
     * Keyboard and mouse state a running play script reads through the `Input` library.
     *
     * The editor fills it from the window and viewport while playing; SceneRuntime callers fill it
     * themselves (or bind a window through the helpers above). Keys are identified by name, so
     * `IsKeyDown("W")` and `IsKeyDown("w")` are the same query.
     *
     * Held state (`down`) persists until the opposite edge arrives. Pressed/released are edge flags
     * that survive until ClearEdges() — PlaySession calls it at the end of every Tick() — so a
     * script sees each press exactly once even though several scripts share the state.
     */
    struct InputState {
        /// Mouse position in world units, already converted from viewport pixels by the feeder.
        float mouseX = 0.0f;
        float mouseY = 0.0f;

        // ---- feeding ---------------------------------------------------------

        /// Records a key going down or up. Only a real transition produces an edge flag.
        void SetKeyDown(const std::string &name, bool down) {
            const std::string key = NormalizeInputName(name);
            if(key.empty())
                return;

            if(down) {
                if(keysDown_.insert(key).second)
                    keysPressed_.insert(key);
            }
            else {
                if(keysDown_.erase(key) != 0)
                    keysReleased_.insert(key);
            }
        }

        /// Records a mouse button going down or up.
        void SetMouseButtonDown(const std::string &button, bool down) {
            const std::string name = NormalizeInputName(button);
            if(name.empty())
                return;

            if(down)
                buttonsDown_.insert(name);
            else
                buttonsDown_.erase(name);
        }

        void SetMousePosition(float worldX, float worldY) {
            mouseX = worldX;
            mouseY = worldY;
        }

        // ---- queries ---------------------------------------------------------

        bool IsKeyDown(const std::string &name) const {
            const std::string key = NormalizeInputName(name);
            return !key.empty() && keysDown_.count(key) != 0;
        }

        bool IsKeyPressed(const std::string &name) const {
            const std::string key = NormalizeInputName(name);
            return !key.empty() && keysPressed_.count(key) != 0;
        }

        bool IsKeyReleased(const std::string &name) const {
            const std::string key = NormalizeInputName(name);
            return !key.empty() && keysReleased_.count(key) != 0;
        }

        bool IsMouseDown(const std::string &button) const {
            const std::string name = NormalizeInputName(button);
            return !name.empty() && buttonsDown_.count(name) != 0;
        }

        float MouseX() const { return mouseX; }
        float MouseY() const { return mouseY; }

        /// Drops the pressed/released edge flags. Held keys and buttons stay down.
        void ClearEdges() {
            keysPressed_.clear();
            keysReleased_.clear();
        }

        /// Forgets everything, including held keys and buttons.
        void Reset() {
            keysDown_.clear();
            keysPressed_.clear();
            keysReleased_.clear();
            buttonsDown_.clear();
            mouseX = 0.0f;
            mouseY = 0.0f;
        }

        const std::set<std::string> &GetKeysDown() const { return keysDown_; }
        const std::set<std::string> &GetKeysPressed() const { return keysPressed_; }
        const std::set<std::string> &GetKeysReleased() const { return keysReleased_; }
        const std::set<std::string> &GetMouseButtonsDown() const { return buttonsDown_; }

    private:
        std::set<std::string> keysDown_;
        std::set<std::string> keysPressed_;
        std::set<std::string> keysReleased_;
        std::set<std::string> buttonsDown_;
    };

} // namespace Gorgon::Editor
