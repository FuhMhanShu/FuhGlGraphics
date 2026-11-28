#pragma once

/// @file binding.hpp
/// @brief G41 2D UI layer: minimal Qt-free signal/property/binding infrastructure for
/// two-way data flow between a backend model and the widgets. Pure CPU — unit-testable.
///
/// Model: a backend value lives in exactly one ui::Property<T> (single source of truth).
/// Widgets expose their own Property<T> and are coupled with bindTwoWay() (or connect the
/// property's onChanged() directly). Property::set() fires only on an actual change and
/// guards against re-entrant loops, so two-way bindings cannot ping-pong.

#include <functional>
#include <utility>
#include <vector>

namespace fuhglgraphics::ui {

/// @brief Simple multicast signal (no arguments). connect() returns a handle for disconnect().
class Signal {
public:
    using Slot = std::function<void()>;

    /// @brief Subscribes; returns a handle (> 0) usable with disconnect().
    int connect(Slot slot) {
        slots_.push_back({++nextId_, std::move(slot)});
        return nextId_;
    }
    /// @brief Unsubscribes a handle from connect() (no-op for unknown handles).
    void disconnect(int handle) {
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].first == handle) {
                slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
    }
    /// @brief Fires all slots (in subscription order).
    void emit() {
        for (const auto& s : slots_)
            if (s.second) s.second();
    }
    std::size_t slotCount() const { return slots_.size(); }

private:
    std::vector<std::pair<int, Slot>> slots_;
    int nextId_ = 0;
};

/// @brief Observable value: set() stores and fires changed() only when the value actually
/// changed; re-entrant set() calls (a slot writing back) are queued once and applied after
/// the current emission, so bindings terminate.
template <typename T>
class Property {
public:
    Property() = default;
    explicit Property(T value) : value_(std::move(value)) {}

    const T& get() const { return value_; }
    /// @brief Sets the value; fires changed() on an actual change (operator==).
    void set(const T& v) {
        if (setting_) {           // re-entrant (a listener writes back): queue once
            queued_ = true;
            queuedValue_ = v;
            return;
        }
        if (v == value_) return;  // no change -> no signal (stops binding loops)
        setting_ = true;
        value_ = v;
        changed_.emit();
        setting_ = false;
        if (queued_) {
            queued_ = false;
            set(queuedValue_);
        }
    }
    Property& operator=(const T& v) {
        set(v);
        return *this;
    }

    /// @brief Change signal; connect a slot to get notified on every actual change.
    Signal& onChanged() { return changed_; }
    const Signal& onChanged() const { return changed_; }

private:
    T value_{};
    Signal changed_;
    bool setting_ = false;
    bool queued_ = false;
    T queuedValue_{};
};

/// @brief Two-way binding between two properties of the same type: a -> b and b -> a.
/// The equality check + re-entrancy guard of Property::set() keep this loop-free.
/// Returns the two subscription handles (for disconnect via the properties' signals).
template <typename T>
std::pair<int, int> bindTwoWay(Property<T>& a, Property<T>& b) {
    const int h1 = a.onChanged().connect([&b, &a] { b.set(a.get()); });
    const int h2 = b.onChanged().connect([&a, &b] { a.set(b.get()); });
    return {h1, h2};
}

/// @brief One-way binding: source changes are pushed into target (target -> source NOT synced).
template <typename T>
int bindOneWay(const Property<T>& source, Property<T>& target) {
    target.set(source.get());
    return const_cast<Property<T>&>(source).onChanged().connect([&source, &target] { target.set(source.get()); });
}

} // namespace fuhglgraphics::ui
