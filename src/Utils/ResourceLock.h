#pragma once

#include <concepts>
#include <mutex>
#include <type_traits>
#include <utility>

template <class T>
class ResourceLock {
public:
    // TValue carries the constness of the ResourceLock the lock was acquired from.
    template <class TValue>
    class BasicLock {
    public:
        BasicLock(std::mutex& mutex, TValue& value) : guard_(mutex), value_(&value) {}

        TValue& operator*() const noexcept { return *value_; }
        TValue* operator->() const noexcept { return value_; }

    private:
        std::lock_guard<std::mutex> guard_;
        TValue* value_;
    };

    using Lock = BasicLock<T>;
    using ConstLock = BasicLock<const T>;

    ResourceLock()
        requires std::is_default_constructible_v<T>
    = default;

    template <class... TArgs>
        requires std::constructible_from<T, TArgs...>
    explicit ResourceLock(std::in_place_t, TArgs&&... args) : value_(std::forward<TArgs>(args)...) {}

    Lock Acquire() { return Lock(mutex_, value_); }
    ConstLock Acquire() const { return ConstLock(mutex_, value_); }

private:
    mutable std::mutex mutex_;
    T value_;
};
