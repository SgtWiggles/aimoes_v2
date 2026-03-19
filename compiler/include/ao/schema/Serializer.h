#pragma once

#include <vector>

#include <ao/pack/ByteStream.h>
#include <ao/utils/Span.h>
#include <ao/utils/Variant.h>

#include <optional>
#include <string>
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
    template <class Stream>
    void serialize(Stream& stream, bool v) {
        stream.bytes(ao::utils::makeByteSpan(v), 1);
    }
    template <class Stream>
    void deserialize(Stream& stream, bool& v) {
        v = false;
        std::byte byte;
        stream.bytes(ao::utils::makeByteSpan(byte), 1);
        if (!stream.ok())
            return;
        v = (byte != std::byte{0});
    }
};

#define INTEGRAL_TYPE_SERIALIZER(Type)                              \
    template <>                                                     \
    struct Serializer<Type> {                                       \
        template <class Stream>                                     \
        void serialize(Stream& stream, Type v) {                    \
            stream.bytes(ao::utils::makeByteSpan(v), sizeof(Type)); \
        }                                                           \
        template <class Stream>                                     \
        void deserialize(Stream& stream, Type& v) {                 \
            stream.bytes(ao::utils::makeByteSpan(v), sizeof(Type)); \
        }                                                           \
    };

INTEGRAL_TYPE_SERIALIZER(uint8_t);
INTEGRAL_TYPE_SERIALIZER(uint16_t);
INTEGRAL_TYPE_SERIALIZER(uint32_t);
INTEGRAL_TYPE_SERIALIZER(uint64_t);

INTEGRAL_TYPE_SERIALIZER(int8_t);
INTEGRAL_TYPE_SERIALIZER(int16_t);
INTEGRAL_TYPE_SERIALIZER(int32_t);
INTEGRAL_TYPE_SERIALIZER(int64_t);

INTEGRAL_TYPE_SERIALIZER(float);
INTEGRAL_TYPE_SERIALIZER(double);

#undef INTEGER_SERIALIZER

template <class T>
struct Serializer<std::vector<T>> {
    template <class Stream>
    void serialize(Stream& stream, std::vector<T> const& v) {
        uint64_t size = v.size();
        stream.bytes(ao::utils::makeByteSpan(size), sizeof(size));
        for (auto const& item : v) {
            Serializer<T>{}.serialize(stream, item);
        }
    }
    template <class Stream>
    void deserialize(Stream& stream, std::vector<T>& v) {
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
    template <class Stream>
    void serialize(Stream& stream, std::variant<Args...> const& v) {
        uint64_t index = v.index();
        stream.bytes(ao::utils::makeByteSpan(index), sizeof(index));
        std::visit(
            [&](auto&& arg) {
                Serializer<std::decay_t<decltype(arg)>>{}.serialize(stream,
                                                                    arg);
            },
            v);
    }
    template <class Stream>
    void deserialize(Stream& stream, std::variant<Args...>& v) {
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
    template <class Stream>
    void serialize(Stream& stream, std::optional<T> const& v) {
        bool present = v.has_value();
        stream.bytes(ao::utils::makeByteSpan(present), 1);
        if (present) {
            Serializer<T>{}.serialize(stream, *v);
        }
    }
    template <class Stream>
    void deserialize(Stream& stream, std::optional<T>& v) {
        v.reset();

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
    template <class Stream>
    void serialize(Stream& stream, T const& v) {
        ao::meta::visit(
            [&stream](auto const& value, auto memberInfo) {
                if (!stream.ok())
                    return;
                using S = Serializer<std::decay_t<decltype(value)>>;
                S{}.serialize(stream, value);
            },
            v);
    }
    template <class Stream>
    void deserialize(Stream& stream, T& v) {
        // Default construct/wipe the value
        v = {};

        ao::meta::visit(
            [&stream](auto& value, auto memberInfo) {
                if (!stream.ok())
                    return;
                using S = Serializer<std::decay_t<decltype(value)>>;
                S{}.deserialize(stream, value);
            },
            v);
    }
};

template <>
struct Serializer<std::string> {
    template <class Stream>
    void serialize(Stream& stream, std::string const& v) {
        uint64_t size = v.size();
        stream.bytes(ao::utils::makeByteSpan(size), sizeof(size));
        stream.bytes(ao::utils::makeByteSpan(v), v.size());
    }
    template <class Stream>
    void deserialize(Stream& stream, std::string& v) {
        uint64_t size = 0;
        stream.bytes(ao::utils::makeByteSpan(size), sizeof(size));
        if (!stream.ok())
            return;
        v.resize(size);
        stream.bytes(ao::utils::makeByteSpan(v), size);
    }
};

template <class EnumType, size_t MaxEnumValue, class Wire = uint64_t>
struct EnumSerializer {
    template <class Stream>
    void serialize(Stream& stream, EnumType v) {
        Serializer<Wire>{}.serialize(stream, (Wire)v);
    }
    template <class Stream>
    void deserialize(Stream& stream, EnumType& v) {
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
