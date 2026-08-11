#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <type_traits>

template <typename T>
    requires std::same_as<T, std::remove_cvref_t<T>> && requires(T* x) {
        { x->next } -> std::convertible_to<T*>;
    }
class PointerLinkedList {
public:
    explicit PointerLinkedList(T** head) : _head(head) {}

    class Iterator {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator() = default;

        explicit Iterator(T* node) : _node(node) {}

        reference operator*() const { return *_node; }

        pointer operator->() const { return _node; }

        Iterator& operator++() {
            _node = _node->next;
            return *this;
        }

        Iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        bool operator==(const Iterator&) const = default;

    private:
        T* _node = nullptr;
    };

    Iterator begin() const { return Iterator(*_head); }

    Iterator end() const { return Iterator(nullptr); }

    Iterator remove(Iterator it) {
        auto* node = it.operator->();
        if (!node || !_head || !*_head) {
            return end();
        }

        auto* next = node->next;

        if (*_head == node) {
            *_head = next;
            return Iterator(next);
        }

        for (auto current = *_head; current; current = current->next) {
            if (current->next == node) {
                current->next = next;
                return Iterator(next);
            }
        }

        return end();
    }

private:
    T** _head;
};
