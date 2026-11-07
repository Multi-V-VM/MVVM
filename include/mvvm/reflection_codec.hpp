#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <meta>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace mvvm::codec {
struct Limits {
    std::uint64_t bytes = 512ULL * 1024 * 1024;
    std::uint64_t elements = 1'000'000;
    unsigned depth = 128;
};
namespace detail {
template<class T> struct vector_type : std::false_type {};
template<class T, class A> struct vector_type<std::vector<T,A>> : std::true_type {};
template<class T> struct unique_type : std::false_type {};
template<class T> struct unique_type<std::unique_ptr<T>> : std::true_type {};
template<class T> struct span_type : std::false_type {};
template<class T, std::size_t N> struct span_type<std::span<T,N>> : std::true_type {};
template<class T> constexpr bool map_type = requires { typename T::key_type; typename T::mapped_type; };
template<class T> constexpr bool tuple_type = requires { typename std::tuple_size<T>::type; };
template<class T> constexpr bool byte_type = sizeof(T)==1 && (std::is_integral_v<T> || std::same_as<T,std::byte>) && !std::same_as<T,bool>;

consteval void hash_text(std::uint64_t &h, std::string_view s) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
}
template<class T> consteval void schema(std::uint64_t &h) {
    using U=std::remove_cv_t<T>;
    hash_text(h,std::meta::display_string_of(^^U));
    if constexpr (vector_type<U>::value || span_type<U>::value) schema<typename U::value_type>(h);
    else if constexpr (unique_type<U>::value) schema<typename U::element_type>(h);
    else if constexpr (map_type<U>) { schema<typename U::key_type>(h); schema<typename U::mapped_type>(h); }
    else if constexpr (std::is_array_v<U>) schema<std::remove_extent_t<U>>(h);
    else if constexpr (tuple_type<U>) {
        [&]<std::size_t... I>(std::index_sequence<I...>) consteval { (schema<std::tuple_element_t<I,U>>(h),...); }(std::make_index_sequence<std::tuple_size_v<U>>{});
    } else if constexpr (std::is_aggregate_v<U> && !std::is_union_v<U>) {
        static_assert(std::meta::bases_of(^^U,std::meta::access_context::current()).empty(), "checkpoint records cannot have base classes");
        template for (constexpr auto m : std::define_static_array(std::meta::nonstatic_data_members_of(^^U,std::meta::access_context::current()))) {
            hash_text(h,std::meta::identifier_of(m));
            schema<typename[:std::meta::type_of(m):]>(h);
        }
    }
}

struct BufferWriter {
    std::vector<char> data;
    bool write(const char* p,std::size_t n) { data.insert(data.end(),p,p+n); return true; }
};
struct BufferReader {
    std::span<const std::byte> data;
    std::size_t offset=0;
    bool read(char* p,std::size_t n) {
        if(n>data.size()-offset) return false;
        if(n) std::memcpy(p,data.data()+offset,n);
        offset+=n; return true;
    }
    // Deliberately no read_view: decoding mutable borrowed spans from const
    // input would discard constness. Use a mutable stream for CXL views.
};
template<class R> struct Input {
    R& stream; Limits limits; std::uint64_t consumed=0; unsigned depth=0;
    void charge(std::size_t n) {
        if(n>limits.bytes-consumed) throw std::runtime_error("checkpoint byte limit exceeded");
        consumed+=n;
    }
    void read(char* p,std::size_t n) {
        charge(n);
        if(n && !stream.read(p,n)) throw std::runtime_error("truncated checkpoint");
    }
};
template<class W> void write(W& w,const char* p,std::size_t n) {
    if(n && !w.write(p,n)) throw std::runtime_error("checkpoint write failed");
}
template<class W, class U> void integer(W& w,U v) {
    static_assert(std::is_unsigned_v<U>);
    if constexpr(std::endian::native==std::endian::big) v=std::byteswap(v);
    write(w,reinterpret_cast<const char*>(&v),sizeof(v));
}
template<class U, class R> U integer(Input<R>& r) {
    static_assert(std::is_unsigned_v<U>);
    U v{}; r.read(reinterpret_cast<char*>(&v),sizeof(v));
    if constexpr(std::endian::native==std::endian::big) v=std::byteswap(v);
    return v;
}
template<class W,class T> void put(W& w,const T& v) {
    using U=std::remove_cv_t<T>;
    if constexpr(std::same_as<U,bool>) integer(w,std::uint8_t(v));
    else if constexpr(std::same_as<U,std::byte>) integer(w,std::to_integer<std::uint8_t>(v));
    else if constexpr(std::is_enum_v<U>) put(w,static_cast<std::underlying_type_t<U>>(v));
    else if constexpr(std::is_integral_v<U>) integer(w,static_cast<std::make_unsigned_t<U>>(v));
    else if constexpr(std::is_floating_point_v<U>) {
        static_assert(sizeof(U)==4 || sizeof(U)==8);
        using Bits=std::conditional_t<sizeof(U)==4,std::uint32_t,std::uint64_t>;
        integer(w,std::bit_cast<Bits>(v));
    } else if constexpr(std::same_as<U,std::string>) {
        integer(w,std::uint64_t(v.size())); write(w,v.data(),v.size());
    } else if constexpr(unique_type<U>::value) { put(w,bool(v)); if(v) put(w,*v); }
    else if constexpr(vector_type<U>::value || span_type<U>::value) {
        integer(w,std::uint64_t(v.size()));
        if constexpr(byte_type<typename U::value_type>) write(w,reinterpret_cast<const char*>(v.data()),v.size());
        else for(const auto& item:v) put(w,item);
    } else if constexpr(map_type<U>) {
        integer(w,std::uint64_t(v.size()));
        for(const auto& [k,x]:v) { put(w,k); put(w,x); }
    } else if constexpr(std::is_array_v<U>) { for(const auto& x:v) put(w,x); }
    else if constexpr(tuple_type<U>) std::apply([&](const auto&... x){(put(w,x),...);},v);
    else if constexpr(std::is_aggregate_v<U> && !std::is_union_v<U>) {
        template for (constexpr auto m : std::define_static_array(std::meta::nonstatic_data_members_of(^^U,std::meta::access_context::current()))) put(w,v.[:m:]);
    } else static_assert(sizeof(U)==0,"unsupported checkpoint type (raw pointers/unions are not portable)");
}
template<class R> std::size_t count(Input<R>& r,bool bytes=false) {
    auto n=integer<std::uint64_t>(r);
    if(n>SIZE_MAX || n>(bytes ? r.limits.bytes-r.consumed : r.limits.elements)) throw std::runtime_error("checkpoint container limit exceeded");
    return static_cast<std::size_t>(n);
}
template<class R,class T> void get(Input<R>& r,T& v) {
    using U=std::remove_cv_t<T>;
    if(++r.depth>r.limits.depth) throw std::runtime_error("checkpoint nesting limit exceeded");
    struct Guard { unsigned& n; ~Guard(){--n;} } guard{r.depth};
    if constexpr(std::same_as<U,bool>) {
        auto b=integer<std::uint8_t>(r); if(b>1) throw std::runtime_error("invalid checkpoint bool"); v=b;
    } else if constexpr(std::same_as<U,std::byte>) v=std::byte(integer<std::uint8_t>(r));
    else if constexpr(std::is_enum_v<U>) { std::underlying_type_t<U> x; get(r,x); v=static_cast<U>(x); }
    else if constexpr(std::is_integral_v<U>) v=std::bit_cast<U>(integer<std::make_unsigned_t<U>>(r));
    else if constexpr(std::is_floating_point_v<U>) {
        static_assert(sizeof(U)==4 || sizeof(U)==8);
        using Bits=std::conditional_t<sizeof(U)==4,std::uint32_t,std::uint64_t>;
        v=std::bit_cast<U>(integer<Bits>(r));
    } else if constexpr(std::same_as<U,std::string>) {
        auto n=count(r,true); v.resize(n); r.read(v.data(),n);
    } else if constexpr(unique_type<U>::value) {
        bool present; get(r,present); v.reset();
        if(present) { v=std::make_unique<typename U::element_type>(); get(r,*v); }
    } else if constexpr(span_type<U>::value) {
        static_assert(byte_type<typename U::value_type>,"only byte spans may borrow serialized storage");
        auto n=count(r,true);
        if constexpr(U::extent!=std::dynamic_extent) if(n!=U::extent) throw std::runtime_error("span extent mismatch");
        r.charge(n);
        if constexpr(requires { r.stream.read_view(n); }) {
            const char* p=n ? r.stream.read_view(n) : nullptr;
            if(n && !p) throw std::runtime_error("truncated checkpoint view");
            // ReadStream's view contract: mutable storage owned by the stream
            // or shared mapping; it must outlive the decoded record.
            v=U(reinterpret_cast<typename U::element_type*>(const_cast<char*>(p)),n);
        } else throw std::runtime_error("borrowed spans require a mutable read_view stream");
    } else if constexpr(vector_type<U>::value) {
        auto n=count(r,byte_type<typename U::value_type>); v.clear();
        if constexpr(byte_type<typename U::value_type>) { v.resize(n); r.read(reinterpret_cast<char*>(v.data()),n); }
        else for(std::size_t i=0;i<n;++i) { typename U::value_type x{}; get(r,x); v.push_back(std::move(x)); }
    } else if constexpr(map_type<U>) {
        auto n=count(r); v.clear();
        for(std::size_t i=0;i<n;++i) {
            typename U::key_type k{}; typename U::mapped_type x{}; get(r,k); get(r,x);
            if(!v.emplace(std::move(k),std::move(x)).second) throw std::runtime_error("duplicate checkpoint map key");
        }
    } else if constexpr(std::is_array_v<U>) { for(auto& x:v) get(r,x); }
    else if constexpr(tuple_type<U>) std::apply([&](auto&... x){(get(r,x),...);},v);
    else if constexpr(std::is_aggregate_v<U> && !std::is_union_v<U>) {
        template for (constexpr auto m : std::define_static_array(std::meta::nonstatic_data_members_of(^^U,std::meta::access_context::current()))) get(r,v.[:m:]);
    } else static_assert(sizeof(U)==0,"unsupported checkpoint type");
}
} // namespace detail

template<class T> consteval std::uint64_t schema_fingerprint() {
    std::uint64_t h=14695981039346656037ULL; detail::schema<T>(h); return h;
}
template<class W,class T> void serialize_to(W& stream,const T& value) {
    detail::integer(stream,std::uint64_t{0x324645524d56564d}); // MVVMREF2, little endian
    detail::integer(stream,schema_fingerprint<T>()); detail::put(stream,value);
}
template<class T> std::vector<char> serialize(const T& value) {
    detail::BufferWriter w; serialize_to(w,value); return std::move(w.data);
}
template<class T,class R> std::expected<T,std::string> deserialize(R& stream,Limits limits={}) {
    try {
        detail::Input<R> r{stream,limits};
        if(detail::integer<std::uint64_t>(r)!=0x324645524d56564d) throw std::runtime_error("unsupported checkpoint format; regenerate with reflection codec v2");
        if(detail::integer<std::uint64_t>(r)!=schema_fingerprint<T>()) throw std::runtime_error("checkpoint schema mismatch");
        T value{}; detail::get(r,value); return value;
    } catch(const std::exception& e) { return std::unexpected(e.what()); }
}
template<class T> std::vector<std::byte> encode(const T& value) {
    const auto bytes=serialize(value); auto view=std::as_bytes(std::span(bytes));
    return {view.begin(),view.end()};
}
template<class T> std::expected<T,std::string> decode(std::span<const std::byte> bytes,Limits limits={}) {
    detail::BufferReader r{bytes}; auto value=deserialize<T>(r,limits);
    if(value && r.offset!=bytes.size()) return std::unexpected("trailing checkpoint bytes");
    return value;
}
} // namespace mvvm::codec
