/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2024 Regents of the Univeristy of California
 *  UC Santa Cruz Sluglab.
 */

#ifndef MVVM_WAMR_SERIALIZER_H
#define MVVM_WAMR_SERIALIZER_H
#include <array>
#include <type_traits>

template <typename T, typename K>
concept SerializerTrait = requires(T &t, K k) {
    { t->dump_impl(k) } -> std::same_as<void>;
    { t->restore_impl(k) } -> std::same_as<void>;
};

template <typename T, typename K>
concept CheckerTrait = requires(T &t, K k) {
    { t->dump_impl(k) } -> std::same_as<void>;
    { t->equal_impl(k) } -> std::convertible_to<bool>;
};

#endif // MVVM_WAMR_SERIALIZER_H
