#pragma once

#include <vector>

#include <ao/pack/ByteStream.h>
#include <ao/utils/Span.h>
#include <ao/utils/Variant.h>

#include <optional>
#include <variant>
#include <vector>

#include <ao/meta/Reflect.h>

namespace ao::schema {
template <class T>
struct Serializer;

template <class Stream, class Value>
void serialize(Stream& stream, Value const& v) {
    Serializer<Value>{}.serialize(stream, v);
}
template <class Stream, class Value>
void deserialize(Stream& stream, Value& v) {
    Serializer<Value>{}.deserialize(stream, v);
}

template <>
struct Serializer<bool> {
    void serialize(ao::pack::byte::WriteStream& stream, bool v) {
        stream.bytes(ao::utils::makeByteSpan(v), 1);
    }
    void deserialize(ao::pack::byte::ReadStream& stream, bool& v) {
        v = false;
        std::byte byte;
        stream.bytes(ao::utils::makeByteSpan(byte), 1);
        if (!stream.ok())
            return;
        v = (byte != std::byte{0});
    }
};

#define INTEGRAL_TYPE_SERIALIZER(Type)                                  \
    template <>                                                         \
    struct Serializer<Type> {                                           \
        void serialize(ao::pack::byte::WriteStream& stream, Type v) {   \
            stream.bytes(ao::utils::makeByteSpan(v), sizeof(Type));     \
        }                                                               \
        void deserialize(ao::pack::byte::ReadStream& stream, Type& v) { \
            stream.bytes(ao::utils::makeByteSpan(v), sizeof(Type));     \
        }                                                               \
    };

INTEGRAL_TYPE_SERIALIZER(uint8_t);
INTEGRAL_TYPE_SERIALIZER(uint16_t);
INTEGRAL_TYPE_SERIALIZER(uint32_t);
INTEGRAL_TYPE_SERIALIZER(uint64_t);

INTEGRAL_TYPE_SERIALIZER(int8_t);
INTEGRAL_TYPE_SERIALIZER(int16_t);
INTEGRAL_TYPE_SERIALIZER(int32_t);
INTEGRAL_TYPE_SERIALIZER(int64_t);

#undef INTEGER_SERIALIZER

template <class T>
struct Serializer<std::vector<T>> {
    void serialize(ao::pack::byte::WriteStream& stream,
                   std::vector<T> const& v) {
        uint64_t size = v.size();
        stream.bytes(ao::utils::makeByteSpan(size), sizeof(size));
        for (auto const& item : v) {
            Serializer<T>{}.serialize(stream, item);
        }
    }
    void deserialize(ao::pack::byte::ReadStream& stream, std::vector<T>& v) {
        uint64_t size = 0;
        stream.bytes(ao::utils::makeByteSpan(size), sizeof(size));
        if (!stream.ok())
            return;
        v.resize(size);
        for (auto& item : v) {
            Serializer<T>{}.deserialize(stream, item);
            if (!stream.ok())
                return;
        }
    }
};

template <class... Args>
struct Serializer<std::variant<Args...>> {
    void serialize(ao::pack::byte::WriteStream& stream,
                   std::variant<Args...> const& v) {
        uint64_t index = v.index();
        stream.bytes(ao::utils::makeByteSpan(index), sizeof(index));
        std::visit(
            [&](auto&& arg) {
                Serializer<std::decay_t<decltype(arg)>>{}.serialize(stream,
                                                                    arg);
            },
            v);
    }
    void deserialize(ao::pack::byte::ReadStream& stream,
                     std::variant<Args...>& v) {
        uint64_t index = 0;
        stream.bytes(ao::utils::makeByteSpan(index), sizeof(index));
        if (!stream.ok())
            return;
        auto success = ao::utils::emplaceIndex(index, v);
        if (!success) {
            stream.require(false, ao::pack::Error::BadArg);
            return;
        }
        std::visit(
            [&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                Serializer<T>{}.deserialize(stream, arg);
            },
            v);
    }
};

template <class T>
struct Serializer<std::optional<T>> {
    void serialize(ao::pack::byte::WriteStream& stream,
                   std::optional<T> const& v) {
        bool present = v.has_value();
        stream.bytes(ao::utils::makeByteSpan(present), 1);
        if (present) {
            Serializer<T>{}.serialize(stream, *v);
        }
    }
    void deserialize(ao::pack::byte::ReadStream& stream, std::optional<T>& v) {
        v.clear();

        bool present = false;
        Serializer<bool>{}.deserialize(stream, present);
        if (!stream.ok())
            return;

        if (!present) {
            return;
        }

        v.emplace();
        Serializer<T>{}.deserialize(stream, *v);
    }
};

template <ao::meta::Reflectable T>
struct Serializer<T> {
    void serialize(ao::pack::byte::WriteStream& stream, T const& v) {
        ao::meta::visit(
            [&stream](auto const& value, auto memberInfo) {
                if (!stream.ok())
                    return;
                Serializer<T>{}.serialize(stream, value);
            },
            v);
    }
    void deserialize(ao::pack::byte::ReadStream& stream, T& v) {
        // Default construct/wipe the value
        v = {};

        ao::meta::visit(
            [&stream](auto& value, auto memberInfo) {
                if (!stream.ok())
                    return;
                Serializer<T>{}.deserialize(stream, value);
            },
            v);
    }
};

template <class EnumType, size_t MaxEnumValue, class Wire = size_t>
struct EnumSerializer {
    void serialize(ao::pack::byte::WriteStream& stream, EnumType v) {
        Serializer<Wire>{}.serialize(stream, (Wire)v);
    }
    void deserialize(ao::pack::byte::ReadStream& stream, EnumType& v) {
        Wire wireValue;
        Serializer<Wire>{}.deserialize(stream, wireValue);
        if (wireValue >= MaxEnumValue) {
            stream.require(false, ao::pack::Error::BadArg);
            return;
        }
        v = (EnumType)wireValue;
    }
};

}  // namespace ao::schema
