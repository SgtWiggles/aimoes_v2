#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ao/pack/Error.h"

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
    void msgBegin(uint32_t msgId);
    void msgEnd();

    void fieldBegin(uint32_t fieldId);
    void fieldEnd();

    bool optPresent();
    void optEnterValue();
    void optExitValue();

    uint32_t arrayLen();
    void arrayEnterElem(uint32_t i);
    void arrayExitElem(uint32_t i);

    uint32_t oneOfIndex(uint32_t oneofId);  // chosen arm index (or -1)
    void oneOfEnterArm(uint32_t arm);
    void oneOfExitArm();

    bool readBool();
    uint64_t readU64();
    int64_t readI64();
    float readF32();
    double readF64();

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
    void optSetPresent(bool present);
    // enter optional's value storage (must be present)
    void optEnterValue();
    void optExitValue();

    // Array:
    // For decode, codec provides length; object adapter must resize/prepare
    // container.
    void arrayPrepare(uint32_t len);
    void arrayEnterElem(uint32_t i);
    void arrayExitElem();

    // Oneof:
    // For decode, codec selects arm; object adapter must set discriminant and
    // prepare arm storage.
    void oneofEnterArm(uint32_t arm);
    void oneofExitArm();

    // Scalars (write into current storage):
    void writeBool(bool v);
    void writeU64(uint64_t v);
    void writeI64(int64_t v);
    void writeF32(float v);
    void writeF64(double v);

    // Status:
    bool ok() const { return m_err == pack::Error::BadArg; }
    ao::pack::Error error() const { return m_err; }

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
    nlohmann::json m_root;
    std::vector<nlohmann::json*> m_stack;

    ao::pack::Error m_err;
};

}  // namespace ao::schema::json
