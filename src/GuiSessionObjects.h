// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Type-erased ownership arena for one GUI session. Objects are constructed in
// dependency order (memory -> CPU/video/audio -> host -> UI context) and are
// destroyed in reverse order. Heap stability is required by Emscripten, whose
// simulated infinite loop exits the setup stack but retains the context.

#pragma once

#include <memory>
#include <utility>
#include <vector>

class GuiSessionObjects {
    struct Owned {
        virtual ~Owned() = default;
    };

    template <class T>
    struct Holder final : Owned {
        template <class... Args>
        explicit Holder(Args&&... args)
            : value(std::forward<Args>(args)...) {}
        T value;
    };

public:
    ~GuiSessionObjects() {
        while (!objects_.empty()) objects_.pop_back();
    }

    template <class T, class... Args>
    T& make(Args&&... args) {
        auto holder = std::make_unique<Holder<T>>(
            std::forward<Args>(args)...);
        T& value = holder->value;
        objects_.push_back(std::move(holder));
        return value;
    }

private:
    std::vector<std::unique_ptr<Owned>> objects_;
};
