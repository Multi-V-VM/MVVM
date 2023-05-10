//
// Created by yiwei yang on 5/4/23.
//

#ifndef MVVM_WAMR_SERIALIZER_H
#define MVVM_WAMR_SERIALIZER_H

#include "wamr_exec_env.h"
#include <array>

template <typename T, typename K>
concept SerializerTrait = requires(T t, K k) {
    { t->dump(k) } -> std::same_as<void>;
    { t->restore(k) } -> std::same_as<void>;
};

#endif // MVVM_WAMR_SERIALIZER_H
