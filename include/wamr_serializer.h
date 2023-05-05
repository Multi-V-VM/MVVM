//
// Created by yiwei yang on 5/4/23.
//

#ifndef MVVM_WAMR_SERIALIZER_H
#define MVVM_WAMR_SERIALIZER_H
class WAMRSerializer {
public:
    virtual void dump() = 0;
    virtual void restore() = 0;
};
#endif //MVVM_WAMR_SERIALIZER_H
