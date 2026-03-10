#include <catch2/catch_all.hpp>

#include <vector>

#include "Helpers.h"
#include "ao/schema/JSONBackend.h"
#include "ao/schema/VM.h"
#include "ao/schema/VMPrettyPrint.h"

using namespace ao::schema::json;
using namespace ao::schema::vm;

namespace {
JsonEncodeState buildJsonState(std::string_view schema) {
    std::string buildErrors;
    auto ir = buildToIR(schema, buildErrors);
    INFO(buildErrors);
    REQUIRE(ir.has_value());

    ao::schema::ErrorContext errs;
    auto state = generateJsonEncodeState(*ir, errs);
    INFO(errs.toString());
    REQUIRE(errs.ok());
    return state;
}

uint64_t requireMessageId(JsonEncodeState const& state,
                          uint64_t messageNumber) {
    auto msgId = state.format.msgs.getId(messageNumber);
    REQUIRE(msgId.has_value());
    return *msgId;
}

nlohmann::json roundTrip(JsonEncodeState const& state,
                         uint64_t messageId,
                         nlohmann::json const& input) {
    std::vector<std::byte> data(4096);
    ao::pack::bit::WriteStream ws{data};
    auto encoded = encodeJson(state, input, ws, messageId);
    {
        INFO("ENCODE " << prettyPrint(state.format.encode));
        REQUIRE(encoded.error == ao::schema::vm::VMError::Ok);
        REQUIRE(ws.ok());
    }

    ao::pack::bit::ReadStream rs{{data.data(), ws.byteSize()}};
    nlohmann::json output{nullptr};
    auto decoded = decodeJson(state, rs, output, messageId);
    {
        INFO("DECODE" << prettyPrint(state.format.decode));
        REQUIRE(decoded.error == ao::schema::vm::VMError::Ok);
        REQUIRE(rs.ok());
        REQUIRE(rs.position().bitPos == ws.bitSize());
    }
    return output;
}
}  // namespace

TEST_CASE("Json codec round trips scalars and nested messages",
          "[json][codec]") {
    auto state = buildJsonState(R"(
package pkg;
message 101 Inner {
    1 count uint(bits=6);
    2 enabled bool;
}
message 100 Test {
    10 hello int(bits=10);
    12 world uint(bits=10);
    14 flag bool;
    16 ratio float;
    18 score double;
    20 inner Inner;
})");

    auto msgId = requireMessageId(state, 100);
    auto input = nlohmann::json::object({
        {"hello", -129},
        {"world", 129},
        {"flag", true},
        {"ratio", 1.5},
        {"score", -3.25},
        {"inner", nlohmann::json::object({
                      {"count", 17},
                      {"enabled", false},
                  })},
    });

    auto output = roundTrip(state, msgId, input);

    REQUIRE(output["hello"].get<int64_t>() == -129);
    REQUIRE(output["world"].get<uint64_t>() == 129);
    REQUIRE(output["flag"].get<bool>() == true);
    REQUIRE(output["ratio"].get<float>() == Catch::Approx(1.5f));
    REQUIRE(output["score"].get<double>() == Catch::Approx(-3.25));
    REQUIRE(output["inner"]["count"].get<uint64_t>() == 17);
    REQUIRE(output["inner"]["enabled"].get<bool>() == false);
}

TEST_CASE("Json codec round trips array of messages", "[json][codec]") {
    auto state = buildJsonState(R"(
package pkg;
message 101 Inner {
    1 value int(bits=7);
}
message 100 Test {
    18 nested array<Inner>;
})");

    auto msgId = requireMessageId(state, 100);

    SECTION("two elements") {
        auto input = nlohmann::json::object({
            {"nested", nlohmann::json::array({
                           nlohmann::json::object({{"value", 5}}),
                           nlohmann::json::object({{"value", -6}}),
                       })},
        });

        auto output = roundTrip(state, msgId, input);
        REQUIRE(output["nested"] == nlohmann::json::array({
                                        nlohmann::json::object({{"value", 5}}),
                                        nlohmann::json::object({{"value", -6}}),
                                    }));
    }

    SECTION("one element") {
        auto input = nlohmann::json::object({
            {"nested", nlohmann::json::array({
                           nlohmann::json::object({{"value", 11}}),
                       })},
        });

        auto output = roundTrip(state, msgId, input);
        REQUIRE(output["nested"] == nlohmann::json::array({
                                        nlohmann::json::object({{"value", 11}}),
                                    }));
    }

    SECTION("empty") {
        auto input = nlohmann::json::object({
            {"nested", nlohmann::json::array()},
        });

        auto output = roundTrip(state, msgId, input);
        REQUIRE(output["nested"] == nlohmann::json::array());
    }
}

TEST_CASE("Json codec round trips optionals oneofs and arrays",
          "[json][codec]") {
    auto state = buildJsonState(R"(
package pkg;
message 101 Inner {
    1 value int(bits=7);
}
message 100 Test {
    10 maybe optional<int(bits=12)>;
    12 maybeInner optional<Inner>;
    14 choice oneof {
        101 asInt int(bits=9);
        102 asBool bool;
        103 asInner Inner;
    };
    16 items array<uint(bits=5)>;
    18 nested array<Inner>;
})");

    auto msgId = requireMessageId(state, 100);

    SECTION("present optionals and scalar oneof arm") {
        auto input = nlohmann::json::object({
            {"maybe", nlohmann::json::object({{"value", -17}})},
            {"maybeInner",
             nlohmann::json::object({
                 {"value", nlohmann::json::object({{"value", 9}})},
             })},
            {"choice", nlohmann::json::object({
                           {"case", 101},
                           {"value", -12},
                       })},
            {"items", nlohmann::json::array({1, 2, 3, 4})},
            {"nested", nlohmann::json::array({
                           nlohmann::json::object({{"value", 5}}),
                           nlohmann::json::object({{"value", -6}}),
                       })},
        });

        auto output = roundTrip(state, msgId, input);

        REQUIRE(output["maybe"]["value"].get<int64_t>() == -17);
        REQUIRE(output["maybeInner"]["value"]["value"].get<int64_t>() == 9);
        REQUIRE(output["choice"]["case"].get<uint64_t>() == 101);
        REQUIRE(output["choice"]["value"].get<int64_t>() == -12);
        REQUIRE(output["items"] == nlohmann::json::array({1, 2, 3, 4}));
        REQUIRE(output["nested"] == nlohmann::json::array({
                                        nlohmann::json::object({{"value", 5}}),
                                        nlohmann::json::object({{"value", -6}}),
                                    }));
    }

    SECTION("absent optionals and message oneof arm") {
        auto input = nlohmann::json::object({
            {"maybe", nullptr},
            {"maybeInner", nullptr},
            {"choice", nlohmann::json::object({
                           {"case", 103},
                           {"value", nlohmann::json::object({{"value", 21}})},
                       })},
            {"items", nlohmann::json::array()},
            {"nested", nlohmann::json::array({
                           nlohmann::json::object({{"value", 11}}),
                       })},
        });

        auto output = roundTrip(state, msgId, input);

        REQUIRE(output["maybe"].is_null());
        REQUIRE(output["maybeInner"].is_null());
        REQUIRE(output["choice"]["case"].get<uint64_t>() == 103);
        REQUIRE(output["choice"]["value"]["value"].get<int64_t>() == 21);
        REQUIRE(output["items"] == nlohmann::json::array());
        REQUIRE(output["nested"] == nlohmann::json::array({
                                        nlohmann::json::object({{"value", 11}}),
                                    }));
    }

    SECTION("boolean oneof arm") {
        auto input = nlohmann::json::object({
            {"maybe", nullptr},
            {"maybeInner", nullptr},
            {"choice", nlohmann::json::object({
                           {"case", 102},
                           {"value", true},
                       })},
            {"items", nlohmann::json::array({7})},
            {"nested", nlohmann::json::array()},
        });

        auto output = roundTrip(state, msgId, input);

        REQUIRE(output["choice"]["case"].get<uint64_t>() == 102);
        REQUIRE(output["choice"]["value"].get<bool>() == true);
        REQUIRE(output["items"] == nlohmann::json::array({7}));
        REQUIRE(output["nested"] == nlohmann::json::array());
    }
}

TEST_CASE("Json codec rejects malformed payloads", "[json][codec]") {
    auto state = buildJsonState(R"(
package pkg;
message 100 Test {
    10 world uint(bits=10);
    12 choice oneof {
        101 asInt int(bits=9);
        102 asBool bool;
    };
    14 items array<uint(bits=5)>;
})");

    auto msgId = requireMessageId(state, 100);

    SECTION("negative uint") {
        std::vector<std::byte> data(1024);
        ao::pack::bit::WriteStream ws{data};
        auto encoded = encodeJson(
            state,
            nlohmann::json::object({
                {"world", -1},
                {"choice",
                 nlohmann::json::object({{"case", 101}, {"value", 3}})},
                {"items", nlohmann::json::array()},
            }),
            ws, msgId);
        REQUIRE_FALSE(encoded.error == ao::schema::vm::VMError::Ok);
    }

    SECTION("unknown oneof case") {
        std::vector<std::byte> data(1024);
        ao::pack::bit::WriteStream ws{data};
        auto encoded = encodeJson(
            state,
            nlohmann::json::object({
                {"world", 1},
                {"choice",
                 nlohmann::json::object({{"case", 999}, {"value", 3}})},
                {"items", nlohmann::json::array()},
            }),
            ws, msgId);
        REQUIRE_FALSE(encoded.error == ao::schema::vm::VMError::Ok);
    }

    SECTION("array must be a json array") {
        std::vector<std::byte> data(1024);
        ao::pack::bit::WriteStream ws{data};
        auto encoded = encodeJson(
            state,
            nlohmann::json::object({
                {"world", 1},
                {"choice",
                 nlohmann::json::object({{"case", 102}, {"value", false}})},
                {"items", 17},
            }),
            ws, msgId);
        REQUIRE_FALSE(encoded.error == ao::schema::vm::VMError::Ok);
    }
}
