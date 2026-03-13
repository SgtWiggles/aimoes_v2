#pragma once
#include <catch2/catch_all.hpp>

#include <cstdint>

#include "ao/schema/IR.h"

#include "ao/schema/JSONBackend.h"
#include "ao/schema/VM.h"
#include "ao/schema/VMPrettyPrint.h"

#include "Helpers.h"

inline ao::schema::json::JsonEncodeState buildJsonState(
    std::string_view schema) {
    std::string buildErrors;
    auto ir = buildToIR(schema, buildErrors);
    INFO(buildErrors);
    REQUIRE(ir.has_value());

    ao::schema::ErrorContext errs;
    auto state = ao::schema::json::generateJsonEncodeState(*ir, errs);
    INFO(errs.toString());
    REQUIRE(errs.ok());
    return state;
}

inline uint64_t requireMessageId(ao::schema::json::JsonEncodeState const& state,
                                 uint64_t messageNumber) {
    auto msgId = state.format.msgs.getId(messageNumber);
    REQUIRE(msgId.has_value());
    return *msgId;
}

template <class WS, class RS>
inline nlohmann::json roundTrip(ao::schema::json::JsonEncodeState const& state,
                                uint64_t messageId,
                                nlohmann::json const& input) {
    std::vector<std::byte> data(4096);
    WS ws{data};
    auto encoded = encodeJson(state, input, ws, messageId);
    {
        INFO("PC " << encoded.pc);
        INFO("ENCODE " << prettyPrint(state.format.encode));
        REQUIRE(encoded.error == ao::schema::vm::VMError::Ok);
        REQUIRE(ws.ok());
    }

    RS rs{{data.data(), ws.byteSize()}};
    nlohmann::json output{nullptr};
    auto decoded = decodeJson(state, rs, output, messageId);
    {
        INFO("PC " << decoded.pc);
        INFO("DECODE" << prettyPrint(state.format.decode));
        REQUIRE(decoded.error == ao::schema::vm::VMError::Ok);
        REQUIRE(rs.ok());
        REQUIRE(rs.remainingBytes() == 0);
    }
    return output;
}
