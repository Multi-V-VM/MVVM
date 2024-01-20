//
// Created by victoryang00 on 5/2/23.
//

#ifndef MVVM_WAMR_WASI_CONTEXT_H
#define MVVM_WAMR_WASI_CONTEXT_H

#include "logging.h"
#include "wamr_export.h"
#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

struct WAMRArgvEnvironValues {
    std::vector<std::string> argv_list;
    std::vector<std::string> env_list;
};
// need to serialize the opened file in the file descripter and the socket opened also.

struct WAMRAddrPool {
    uint8 ip4[4];
    uint16 ip6[8];
    bool is_4;
    uint8 mask;
};
struct WAMRWasiAddr {
    WAMRAddrPool ip;
    uint16 port;
};
struct WasiSockOpenData {
    uint32 poolfd;
    int af;
    int socktype;
    uint32 sockfd;
};
#if !defined(_WIN32)
struct WasiSockSendToData {
    uint32 sock;
    iovec_app_t si_data;
    uint32 si_data_len;
    uint16_t si_flags;
    WAMRWasiAddr dest_addr;
    uint32 so_data_len;
};

struct WasiSockRecvFromData {
    uint32_t sock;
    iovec_app_t ri_data;
    uint32 ri_data_len;
    uint16_t ri_flags;
    WAMRWasiAddr src_addr;
    uint32 ro_data_len;
};
#endif
struct SocketMetaData {
    int domain{};
    int type{};
    int protocol{};
    SocketAddrPool socketAddress{};
    WasiSockOpenData socketOpenData{};
#if !defined(_WIN32)
    WasiSockSendToData socketSentToData{}; // on the fly
    WasiSockRecvFromData socketRecvFromData{}; // on the fly
#endif
};
struct WAMRWASIContext {
    std::map<int, std::tuple<std::string, std::vector<std::tuple<int,int,fd_op>>>>  fd_map;
    std::map<int, SocketMetaData> socket_fd_map;
    std::vector<struct sync_op_t> sync_ops;
    std::vector<std::string> dir;
    std::vector<std::string> map_dir;
    WAMRArgvEnvironValues argv_environ;
    std::vector<WAMRAddrPool> addr_pool;
    std::vector<std::string> ns_lookup_list;
    uint32_t exit_code;
    void dump_impl(WASIArguments *env);
    void restore_impl(WASIArguments *env);
};
template <SerializerTrait<WASIArguments *> T> void dump(T t, WASIArguments *env) { t->dump_impl(env); }
template <SerializerTrait<WASIArguments *> T> void restore(T t, WASIArguments *env) { t->restore_impl(env); }

#endif // MVVM_WAMR_WASI_CONTEXT_H
