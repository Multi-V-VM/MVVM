#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace mvvm::codec {

namespace detail {

template <class T>
using bare_t = std::remove_cvref_t<T>;

template <class T>
struct is_std_vector : std::false_type {};

template <class T, class Allocator>
struct is_std_vector<std::vector<T, Allocator>> : std::true_type {};

template <class T>
inline constexpr bool is_std_vector_v = is_std_vector<bare_t<T>>::value;

template <class T>
struct is_std_array : std::false_type {};

template <class T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {};

template <class T>
inline constexpr bool is_std_array_v = is_std_array<bare_t<T>>::value;

template <class T>
constexpr T byteswap_if_needed(T value) {
    static_assert(std::is_unsigned_v<T>);
    if constexpr (std::endian::native == std::endian::big)
        return std::byteswap(value);
    return value;
}

class Encoder {
  public:
    std::vector<std::byte> output;

    template <class T>
    void scalar(T value) {
        using U = bare_t<T>;
        if constexpr (std::same_as<U, bool>) {
            output.push_back(value ? std::byte{1} : std::byte{0});
        } else if constexpr (std::same_as<U, std::byte>) {
            output.push_back(value);
        } else if constexpr (std::is_enum_v<U>) {
            scalar(static_cast<std::underlying_type_t<U>>(value));
        } else if constexpr (std::is_integral_v<U>) {
            using Unsigned = std::make_unsigned_t<U>;
            const auto little = byteswap_if_needed(static_cast<Unsigned>(value));
            const auto bytes = std::as_bytes(std::span{&little, 1});
            output.insert(output.end(), bytes.begin(), bytes.end());
        } else if constexpr (std::is_floating_point_v<U>) {
            if constexpr (sizeof(U) == sizeof(std::uint32_t))
                scalar(std::bit_cast<std::uint32_t>(value));
            else if constexpr (sizeof(U) == sizeof(std::uint64_t))
                scalar(std::bit_cast<std::uint64_t>(value));
            else
                static_assert(sizeof(U) == 0, "unsupported floating-point width");
        }
    }
};

class Decoder {
  public:
    explicit Decoder(std::span<const std::byte> input) : input_{input} {}

    [[nodiscard]] std::size_t offset() const { return offset_; }
    [[nodiscard]] bool finished() const { return offset_ == input_.size(); }

    template <class T>
    std::expected<T, std::string> scalar() {
        using U = bare_t<T>;
        if constexpr (std::same_as<U, bool>) {
            if (offset_ == input_.size())
                return std::unexpected(std::format("truncated bool at byte {}", offset_));
            const auto value = std::to_integer<unsigned int>(input_[offset_++]);
            if (value > 1)
                return std::unexpected(std::format("invalid bool {} at byte {}", value, offset_ - 1));
            return value != 0;
        } else if constexpr (std::same_as<U, std::byte>) {
            if (offset_ == input_.size())
                return std::unexpected(std::format("truncated byte at byte {}", offset_));
            return input_[offset_++];
        } else if constexpr (std::is_enum_v<U>) {
            auto raw = scalar<std::underlying_type_t<U>>();
            if (!raw)
                return std::unexpected(raw.error());
            return static_cast<U>(*raw);
        } else if constexpr (std::is_integral_v<U>) {
            using Unsigned = std::make_unsigned_t<U>;
            if (input_.size() - offset_ < sizeof(Unsigned))
                return std::unexpected(std::format("truncated {}-byte scalar at byte {}", sizeof(Unsigned), offset_));
            Unsigned value{};
            std::memcpy(&value, input_.data() + offset_, sizeof(value));
            offset_ += sizeof(value);
            return static_cast<U>(byteswap_if_needed(value));
        } else if constexpr (std::is_floating_point_v<U>) {
            if constexpr (sizeof(U) == sizeof(std::uint32_t)) {
                auto raw = scalar<std::uint32_t>();
                if (!raw)
                    return std::unexpected(raw.error());
                return std::bit_cast<U>(*raw);
            } else if constexpr (sizeof(U) == sizeof(std::uint64_t)) {
                auto raw = scalar<std::uint64_t>();
                if (!raw)
                    return std::unexpected(raw.error());
                return std::bit_cast<U>(*raw);
            } else {
                static_assert(sizeof(U) == 0, "unsupported floating-point width");
            }
        }
    }

  private:
    std::span<const std::byte> input_;
    std::size_t offset_{};
};

template <class T>
void encode_value(Encoder &encoder, const T &value);

template <class T>
std::expected<void, std::string> decode_value(Decoder &decoder, T &value);

template <class T>
void encode_value(Encoder &encoder, const T &value) {
    using U = bare_t<T>;
    if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || std::same_as<U, std::byte>) {
        encoder.scalar(value);
    } else if constexpr (std::same_as<U, std::string>) {
        encoder.scalar(static_cast<std::uint64_t>(value.size()));
        const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
        encoder.output.insert(encoder.output.end(), bytes.begin(), bytes.end());
    } else if constexpr (is_std_vector_v<U>) {
        encoder.scalar(static_cast<std::uint64_t>(value.size()));
        for (const auto &item : value)
            encode_value(encoder, item);
    } else if constexpr (is_std_array_v<U>) {
        for (const auto &item : value)
            encode_value(encoder, item);
    } else if constexpr (std::is_aggregate_v<U>) {
        template for (constexpr auto member : std::define_static_array(
                          std::meta::nonstatic_data_members_of(
                              ^^U, std::meta::access_context::current()))) {
            encode_value(encoder, value.[:member:]);
        }
    } else {
        static_assert(std::is_aggregate_v<U>, "type is not supported by the C++26 reflection codec");
    }
}

template <class T>
std::expected<void, std::string> decode_value(Decoder &decoder, T &value) {
    using U = bare_t<T>;
    if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || std::same_as<U, std::byte>) {
        auto decoded = decoder.scalar<U>();
        if (!decoded)
            return std::unexpected(decoded.error());
        value = *decoded;
    } else if constexpr (std::same_as<U, std::string>) {
        auto size = decoder.scalar<std::uint64_t>();
        if (!size)
            return std::unexpected(size.error());
        if (*size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            return std::unexpected("string size does not fit size_t");
        value.resize(static_cast<std::size_t>(*size));
        for (char &item : value) {
            auto byte = decoder.scalar<std::byte>();
            if (!byte)
                return std::unexpected(byte.error());
            item = static_cast<char>(std::to_integer<unsigned char>(*byte));
        }
    } else if constexpr (is_std_vector_v<U>) {
        auto size = decoder.scalar<std::uint64_t>();
        if (!size)
            return std::unexpected(size.error());
        if (*size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            return std::unexpected("vector size does not fit size_t");
        value.clear();
        value.resize(static_cast<std::size_t>(*size));
        for (auto &item : value) {
            auto result = decode_value(decoder, item);
            if (!result)
                return result;
        }
    } else if constexpr (is_std_array_v<U>) {
        for (auto &item : value) {
            auto result = decode_value(decoder, item);
            if (!result)
                return result;
        }
    } else if constexpr (std::is_aggregate_v<U>) {
        std::expected<void, std::string> result{};
        template for (constexpr auto member : std::define_static_array(
                          std::meta::nonstatic_data_members_of(
                              ^^U, std::meta::access_context::current()))) {
            if (result)
                result = decode_value(decoder, value.[:member:]);
        }
        if (!result)
            return result;
    } else {
        static_assert(std::is_aggregate_v<U>, "type is not supported by the C++26 reflection codec");
    }
    return {};
}

constexpr std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace detail

template <class T>
consteval std::uint64_t schema_fingerprint() {
    return detail::fnv1a(std::meta::display_string_of(^^T));
}

template <class T>
std::vector<std::byte> encode(const T &value) {
    detail::Encoder encoder;
    encoder.scalar(schema_fingerprint<T>());
    detail::encode_value(encoder, value);
    return std::move(encoder.output);
}

template <class T>
std::expected<T, std::string> decode(std::span<const std::byte> bytes) {
    detail::Decoder decoder{bytes};
    auto schema = decoder.scalar<std::uint64_t>();
    if (!schema)
        return std::unexpected(schema.error());
    if (*schema != schema_fingerprint<T>())
        return std::unexpected(std::format("checkpoint schema mismatch: got 0x{:016x}, expected 0x{:016x}",
                                           *schema, schema_fingerprint<T>()));
    T value{};
    auto result = detail::decode_value(decoder, value);
    if (!result)
        return std::unexpected(result.error());
    if (!decoder.finished())
        return std::unexpected(std::format("{} trailing checkpoint bytes", bytes.size() - decoder.offset()));
    return value;
}

} // namespace mvvm::codec
