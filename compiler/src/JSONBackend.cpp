#include "ao/schema/JSONBackend.h"

namespace ao::schema::json {
void JsonEncodeAdapter::msgBegin(uint32_t msgId) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    if (!top->is_object())
        return fail(pack::Error::BadData);
    // Do nothing, comes from field/root
}
void JsonEncodeAdapter::msgEnd() {
    // Do nothing, comes from field/root
}
void JsonEncodeAdapter::JsonEncodeAdapter::fieldBegin(uint32_t fieldId) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    auto const& name = fieldKey(fieldId);
    auto nameIter = top->find(name);
    if (nameIter == top->end()) {
        m_stack.push_back(&m_null);
    } else {
        m_stack.push_back(&nameIter.value());
    }
}
void JsonEncodeAdapter::JsonEncodeAdapter::fieldEnd() {
    popStack();
}
bool JsonEncodeAdapter::JsonEncodeAdapter::optPresent() {
    if (!ok())
        return false;
    auto top = currentMsg();
    if (!top)
        return false;
    if (top->is_null())
        return false;
    if (!top->is_object()) {
        fail(pack::Error::BadData);
        return false;
    }
    return top->find("value") != top->end();
}
void JsonEncodeAdapter::JsonEncodeAdapter::optEnterValue() {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    if (top->is_null())
        return fail(pack::Error::BadData);
    if (!top->is_object())
        return fail(pack::Error::BadData);
    auto valueIter = top->find("value");
    if (valueIter == top->end())
        return fail(pack::Error::BadData);
    m_stack.push_back(&valueIter.value());
}
void JsonEncodeAdapter::optExitValue() {
    popStack();
}

uint32_t JsonEncodeAdapter::arrayLen() {
    if (!ok())
        return 0;
    auto top = currentMsg();
    if (!top)
        return 0;
    if (!top->is_array()) {
        fail(pack::Error::BadData);
        return 0;
    }
    return top->size();
}
void JsonEncodeAdapter::arrayEnterElem(uint32_t i) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    if (!top->is_array())
        return fail(pack::Error::BadData);
    if (i >= top->size())
        return fail(pack::Error::BadData);
    m_stack.push_back(&top[i]);
}
void JsonEncodeAdapter::JsonEncodeAdapter::arrayExitElem(uint32_t i) {
    popStack();
}
uint32_t JsonEncodeAdapter::oneOfIndex(uint32_t oneofId) {
    if (!ok())
        return 0;
    auto top = currentMsg();
    if (!top)
        return 0;
    if (!top->contains("case")) {
        fail(pack::Error::BadData);
        return 0;
    }

    auto const& caseNumber = top->at("case");
    if (!caseNumber.is_number_unsigned()) {
        fail(pack::Error::BadData);
        return 0;
    }

    auto caseFieldNum = caseNumber.get<uint32_t>();
    auto& fields = m_table.oneofs[oneofId].fieldNumbers;
    for (uint16_t i = 0; i < fields.size(); ++i) {
        if (fields[i] == caseFieldNum)
            return i;
    }

    fail(pack::Error::BadData);
    return 0;
}
void JsonEncodeAdapter::oneOfEnterArm(uint32_t armId) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    if (!top->contains("value"))
        return fail(pack::Error::BadData);
    m_stack.push_back(&top->at("value"));
}
void JsonEncodeAdapter::oneOfExitArm() {
    popStack();
}
bool JsonEncodeAdapter::readBool() {
    if (!ok())
        return false;
    auto top = currentMsg();
    if (!top)
        return false;
    if (!top->is_boolean()) {
        fail(pack::Error::BadData);
        return false;
    }
    return top->get<bool>();
}
uint64_t JsonEncodeAdapter::readU64() {
    if (!ok())
        return 0;
    auto top = currentMsg();
    if (!top)
        return 0;
    if (top->is_number_unsigned()) {
        return top->get<uint64_t>();
    } else if (top->is_number_integer()) {
        auto val = top->get<int64_t>();
        if (val < 0) {
            fail(pack::Error::BadData);
            return 0;
        }

        return static_cast<uint64_t>(val);
    } else {
        fail(pack::Error::BadData);
        return 0;
    }
}
int64_t JsonEncodeAdapter::readI64() {
    if (!ok())
        return 0;
    auto top = currentMsg();
    if (!top)
        return 0;
    if (!top->is_number_integer()) {
        fail(pack::Error::BadData);
        return 0;
    }
    return top->get<int64_t>();
}
float JsonEncodeAdapter::readF32() {
    if (!ok())
        return 0;
    auto top = currentMsg();
    if (!top)
        return 0;
    if (!top->is_number()) {
        fail(pack::Error::BadData);
        return 0;
    }
    return top->get<float>();
}
double JsonEncodeAdapter::readF64() {
    if (!ok())
        return 0;
    auto top = currentMsg();
    if (!top)
        return 0;
    if (!top->is_number()) {
        fail(pack::Error::BadData);
        return 0;
    }
    return top->get<double>();
}

void JsonDecodeAdapter::msgBegin(uint32_t msgId) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = nlohmann::json::object();
}
void JsonDecodeAdapter::msgEnd() {}

void JsonDecodeAdapter::fieldBegin(uint32_t fieldId) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    if (!top->is_object())
        return fail(pack::Error::BadData);
    auto name = fieldKey(fieldId);
    auto& next = ((*top)[name] = nlohmann::json{nullptr});
    m_stack.push_back(&next);
}
void JsonDecodeAdapter::fieldEnd() {
    popStack();
}

void JsonDecodeAdapter::optSetPresent(bool present) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = nlohmann::json::object({
        {"value", nlohmann::json{nullptr}},
    });
}
void JsonDecodeAdapter::optEnterValue() {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    auto iter = top->find("value");
    if (iter == top->end())
        return fail(pack::Error::BadArg);
    m_stack.push_back(&iter.value());
}
void JsonDecodeAdapter::optExitValue() {
    popStack();
}

void JsonDecodeAdapter::arrayPrepare(uint32_t len) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = nlohmann::json::array();
    top->get_ptr<nlohmann::json::array_t*>()->resize(len);
}
void JsonDecodeAdapter::arrayEnterElem(uint32_t i) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    m_stack.push_back(&(*top)[i]);
}
void JsonDecodeAdapter::arrayExitElem() {
    popStack();
}

void JsonDecodeAdapter::oneofEnterArm(uint32_t arm) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = nlohmann::json::object({
        {"case", m_table.fields[arm].fieldNumber},
        {"value", nlohmann::json{nullptr}},
    });
    m_stack.push_back(&(*top)["value"]);
}
void JsonDecodeAdapter::oneofExitArm() {
    popStack();
}

void JsonDecodeAdapter::writeBool(bool v) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = v;
}
void JsonDecodeAdapter::writeU64(uint64_t v) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = v;
}
void JsonDecodeAdapter::writeI64(int64_t v) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = v;
}
void JsonDecodeAdapter::writeF32(float v) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = v;
}
void JsonDecodeAdapter::writeF64(double v) {
    if (!ok())
        return;
    auto top = currentMsg();
    if (!top)
        return;
    *top = v;
}
JsonTable generateJsonTable(ir::IR const& ir) {
    auto ret = JsonTable{};
    ret.strings = ir.strings;

    ret.fields.reserve(ir.fields.size());
    for (auto& field : ir.fields) {
        ret.fields.emplace_back(JsonField{
            .nameIdx = field.name.idx,
            .fieldNumber = field.fieldNumber,
            .flags = 0,  // TODO these fields later?
        });
    }

    ret.oneofs.reserve(ir.oneOfs.size());
    for (auto& oneof : ir.oneOfs) {
        auto arms = JsonOneOf{};
        for (auto const& arm : oneof.arms)
            arms.fieldNumbers.push_back(ir.fields[arm.idx].fieldNumber);
        ret.oneofs.emplace_back(std::move(arms));
    }
    return ret;
}
}  // namespace ao::schema::json
