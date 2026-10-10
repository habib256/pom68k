// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// « Taper du texte » — host text typed into the guest as key presses
// (TextTyping.h). The window holds the text (pasted from the host
// clipboard or edited), the guest layout chosen EXPLICITLY, a preview of
// what will be typed and of what cannot be, and the machine's progress
// with a cancel; and the other way, the guest's TEXT scrap read back
// (GuestScrap.h) and copied to the host clipboard. Typing itself happens on the machine thread, in machine
// time; the window only queues the request. Gated headlessly by
// gui_machine_window_test.

#pragma once

#include "GuestScrap.h"
#include "TextTyping.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace pom68k::gui {

inline constexpr const char* kTypingWindowTitle = "Taper du texte";

struct GuiTypingState {
    std::function<void(std::string, GuestLayout)> type;
    std::function<void()> cancel;
    std::function<std::size_t()> charactersLeft;
    // Where « Coller » reads the host clipboard; ImGui's by default, so the
    // headless gate can hand it a text without a window system.
    std::function<std::string()> hostClipboard;
    std::function<void(std::string)> setHostClipboard; // ImGui's by default
    // The guest's TEXT scrap: request a read, then the last result and its
    // sequence number (a new number = the answer to the request).
    std::function<void()> readScrap;
    std::function<std::pair<GuestScrapText, unsigned>()> scrap;
    unsigned scrapAsked = 0;
    bool showWindow = false;
    int layout = int(GuestLayout::Us);
    std::vector<char> text = std::vector<char>(kMaxTypedCharacters * 4 + 1, 0);

    bool bound() const noexcept { return bool(type); }
};

template <class MachineT>
void bindTyping(GuiTypingState& typing, MachineT& machine) {
    if constexpr (requires { machine.requestTyping(std::string(), GuestLayout::Us); }) {
        typing.type = [&machine](std::string text, GuestLayout layout) {
            machine.requestTyping(std::move(text), layout);
        };
        typing.cancel = [&machine] { machine.requestCancelTyping(); };
        typing.charactersLeft = [&machine] { return machine.typingLeft(); };
    }
    if constexpr (requires { machine.requestScrapRead(); machine.guestScrap(); }) {
        typing.readScrap = [&machine] { machine.requestScrapRead(); };
        typing.scrap = [&machine] { return machine.guestScrap(); };
    }
}

void drawTypingWindow(GuiTypingState& typing);

} // namespace pom68k::gui
