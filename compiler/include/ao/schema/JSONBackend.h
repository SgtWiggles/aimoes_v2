#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ao/pack/Error.h"
#include "ao/schema/IR.h"
#include "ao/schema/VM.h"

#include "Codec.h"
#include "ao/pack/BitStream.h"

namespace ao::schema::json {
struct JsonField {
    size_t nameIdx;  // points into strings
    uint64_t fieldNumber;
    uint8_t flags;
};
struct JsonOneOf {
    std::vector<uint32_t> fieldNumbers;
};

struct JsonTable {
    std::vector<JsonField> fields;
    std::vector<std::string> strings;
    std::vector<JsonOneOf> oneofs;
};

// Encodes to codec
class JsonEncodeAdapter {
   public:
    JsonEncodeAdapter(JsonTable const& table, nlohmann::json const& root)
        : m_table(table), m_root(root) {
        m_stack.push_back(&m_root);
    }

    void msgBegin(uint32_t msgId);
    void msgEnd();

    void fieldBegin(uint32_t fieldId);
    void fieldEnd();

    bool optPresent();
    void optEnter() {}
    void optExit() {}
    void optEnterValue();
    void optExitValue();

    uint32_t arrayLen();
    void arrayEnterElem(uint32_t i);
    void arrayExitElem();

    uint32_t oneofIndex(uint32_t oneofId);  // chosen arm index (or -1)

    void oneofEnter(uint32_t oneofId);
    void oneofExit();
    void oneofEnterArm(uint32_t oneofId, uint32_t armId);
    void oneofExitArm();

    bool boolean();
    uint64_t u64(uint16_t width);
    int64_t i64(uint16_t width);
    float f32();
    double f64();

    bool ok() const { return m_err != ao::pack::Error::Ok; }
    ao::pack::Error error() const { return m_err; }

   private:
    void fail(ao::pack::Error err) {
        if (ok())
            m_err = err;
    }
    nlohmann::json const* currentMsg() {
        if (!ok())
            return nullptr;
        if (m_stack.empty()) {
            fail(ao::pack::Error::BadData);
            return nullptr;
        }
        return m_stack.back();
    }
    void popStack() {
        if (!ok())
            return;
        if (m_stack.empty()) {
            fail(ao::pack::Error::BadData);
            return;
        }
        m_stack.pop_back();
    }
    std::string_view fieldKey(uint64_t fieldId) {
        return m_table.strings[m_table.fields[fieldId].nameIdx];
    }

    JsonTable const& m_table;
    nlohmann::json const& m_root;
    nlohmann::json m_null = nlohmann::json{nullptr};
    std::vector<nlohmann::json const*> m_stack;
    ao::pack::Error m_err;
};

// Decodes from codec
class JsonDecodeAdapter {
   public:
    JsonDecodeAdapter(JsonTable const& table) : m_table(table) {
        m_stack.push_back(&m_root);
    }
    // Message navigation:
    void msgBegin(uint32_t msgId);
    void msgEnd();

    // Field navigation:
    void fieldBegin(uint32_t fieldId);
    void fieldEnd();

    // Optional:
    // For optional decode, codec typically reads "present bit" and VM branches;
    // object adapter must allocate/set the optional storage before decoding
    // inner value.
    // prepare optional storage (e.g., emplace or reset)
    void optEnter() {}
    void optExit() {}

    void optSetPresent(bool present);
    // enter optional's value storage (must be present)

    void optEnterValue();
    void optExitValue();

    // Array:
    // For decode, codec provides length; object adapter must resize/prepare
    // container.
    void arrayBegin() {}
    void arrayEnd() {}
    void arrayPrepare(uint32_t len);
    void arrayEnterElem(uint32_t i);
    void arrayExitElem();

    // Oneof:
    // For decode, codec selects arm; object adapter must set discriminant and
    // prepare arm storage.
    void oneofEnter(uint32_t oneofId);
    void oneofExit();
    void oneofIndex(uint32_t oneofId, uint32_t armId);

    void oneofEnterArm(uint32_t oneofId, uint32_t armId);
    void oneofExitArm();

    // Scalars (write into current storage):
    void boolean(bool v);
    void u64(uint16_t width, uint64_t v);
    void i64(uint16_t width, int64_t v);
    void f32(float v);
    void f64(double v);

    // Status:
    bool ok() const { return m_err == pack::Error::Ok; }
    ao::pack::Error error() const { return m_err; }

    nlohmann::json root() const { return m_root; }

   private:
    void fail(ao::pack::Error err) {
        if (ok())
            m_err = err;
    }
    nlohmann::json* currentMsg() {
        if (!ok())
            return nullptr;
        if (m_stack.empty()) {
            fail(ao::pack::Error::BadData);
            return nullptr;
        }
        return m_stack.back();
    }
    void popStack() {
        if (!ok())
            return;
        if (m_stack.empty()) {
            fail(ao::pack::Error::BadData);
            return;
        }
        m_stack.pop_back();
    }
    std::string_view fieldKey(uint64_t fieldId) {
        return m_table.strings[m_table.fields[fieldId].nameIdx];
    }

    JsonTable const& m_table;
    nlohmann::json m_root = {};
    std::vector<nlohmann::json*> m_stack = {};

    ao::pack::Error m_err = {};
};

JsonTable generateJsonTable(ir::IR const& ir);

struct JsonEncodeState {
    vm::Format format;
    vm::CodecTable codec;
    JsonTable json;
};

JsonEncodeState generateJsonEncodeState(ir::IR const& ir, ErrorContext& errs);

inline bool encodeJson(JsonEncodeState const& state,
                       nlohmann::json const& json,
                       pack::bit::WriteStream& stream,
                       uint64_t messageId) {
    JsonEncodeAdapter object{state.json, json};
    vm::NetEncodeCodec<pack::bit::WriteStream> codec{
        stream,
        state.codec,
    };
    auto machine = vm::VM{&state.format.encode, object, codec};
    return vm::encode(machine, messageId);
}
inline bool decodeJson(JsonEncodeState const& state,
                       pack::bit::ReadStream& stream,
                       nlohmann::json& json,
                       uint64_t messageId) {
    JsonDecodeAdapter object{state.json};
    vm::NetDecodeCodec<pack::bit::ReadStream> codec{
        stream,
        state.codec,
    };
    auto machine = vm::VM{&state.format.decode, object, codec};
    auto success = vm::decode(machine, messageId);
    if (success) {
        json = object.root();
    }
    return success;
}

}  // namespace ao::schema::json
