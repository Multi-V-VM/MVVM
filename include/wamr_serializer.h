//
// Created by yiwei yang on 5/4/23.
//

#ifndef MVVM_WAMR_SERIALIZER_H
#define MVVM_WAMR_SERIALIZER_H

template <typename T>
concept SerializerTrait = requires(T t) {
    { t.dump_impl() } -> std::same_as<void>;
    { t.restore_impl() } -> std::same_as<void>;
};
#endif // MVVM_WAMR_SERIALIZER_H
