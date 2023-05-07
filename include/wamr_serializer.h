//
// Created by yiwei yang on 5/4/23.
//

#ifndef MVVM_WAMR_SERIALIZER_H
#define MVVM_WAMR_SERIALIZER_H
template <typename Derived> class WAMRSerializer {
public:
    void dump() { static_cast<Derived *>(this)->dump(); };
    void restore() { static_cast<Derived *>(this)->restore(); };
};

template <typename T>
concept SerializerTrait = requires(T t) {
    { t.dump() } -> std::same_as<void>;
    { t.restore() } -> std::same_as<void>;
};
#endif // MVVM_WAMR_SERIALIZER_H
