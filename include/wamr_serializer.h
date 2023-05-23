//
// Created by yiwei yang on 5/4/23.
//

#ifndef MVVM_WAMR_SERIALIZER_H
#define MVVM_WAMR_SERIALIZER_H
#include <array>
#include <type_traits>

template <typename T, typename K>
concept SerializerTrait = requires(T &t, K k) {
    { t->dump(k) } -> std::same_as<void>;
    { t->restore(k) } -> std::same_as<void>;
};

template <typename T, typename K>
concept CheckerTrait = requires(T &t, K k) {
    { t->dump(k) } -> std::same_as<void>;
    { t->equal(k) } -> std::convertible_to<bool>;
};

#endif // MVVM_WAMR_SERIALIZER_H
