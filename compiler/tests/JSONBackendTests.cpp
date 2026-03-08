#include <catch2/catch_all.hpp>

#include <string_view>

#include "Helpers.h"
#include "ao/schema/JSONBackend.h"

using namespace ao::schema::json;

std::optional<uint32_t> findFieldByName(JsonTable const& table,
                                        std::string_view name) {
    for (uint32_t i = 0; i < table.fields.size(); ++i) {
        if (table.strings[table.fields[i].nameIdx] == name)
            return i;
    }

    return {};
}

TEST_CASE("Basic json test", "[json]") {
    std::string buildErrors;
    auto ir = buildToIR(R"(
package a;
message 42 test{
    1 hello int(bits=10);
    2 hello2 uint(bits=11);
}
)",
                        buildErrors);
    INFO(buildErrors);
    REQUIRE(ir.has_value());

    auto jsonTable = generateJsonTable(*ir);
    auto data = nlohmann::json::object({
        {"hello", 13},
        {"hello2", 14},
    });
    auto encoder = JsonEncodeAdapter{
        jsonTable,
        data,
    };
    auto decoder = JsonDecodeAdapter{
        jsonTable,
    };

    // doesn't use the msgid
    encoder.msgBegin(0);
    REQUIRE(encoder.ok());

    encoder.fieldBegin(0);
    REQUIRE(encoder.ok());

    REQUIRE(encoder.i64(0) == 13);
    REQUIRE(encoder.ok());

    encoder.fieldEnd();
    REQUIRE(encoder.ok());

    encoder.fieldBegin(1);
    REQUIRE(encoder.ok());

    auto readVal = encoder.u64(0);
    REQUIRE(readVal == 14);
    REQUIRE(encoder.ok());

    encoder.fieldEnd();
    REQUIRE(encoder.ok());

    encoder.msgEnd();
    REQUIRE(encoder.ok());

    decoder.msgBegin(0);
    REQUIRE(decoder.ok());

    decoder.fieldBegin(0);
    REQUIRE(decoder.ok());
    decoder.i64(0, 13);
    REQUIRE(decoder.ok());
    decoder.fieldEnd();
    REQUIRE(decoder.ok());

    decoder.fieldBegin(1);
    REQUIRE(decoder.ok());
    decoder.i64(0, 14);
    REQUIRE(decoder.ok());
    decoder.fieldEnd();
    REQUIRE(decoder.ok());

    decoder.msgEnd();
    REQUIRE(decoder.ok());

    auto decodedRoot = decoder.root();
    REQUIRE(decodedRoot == data);
}

TEST_CASE("Basic json test with oneof and array", "[json]") {
    std::string buildErrors;
    auto ir = buildToIR(R"(
package a;
message 42 test{
    1024 hello oneof {
        123 subhello1 int;
        456 subhello2 bool;
        789 subhello3 uint;
    };
    2231 hello2 array<uint(bits=11)>;
}
)",
                        buildErrors);
    INFO(buildErrors);
    REQUIRE(ir.has_value());

    auto jsonTable = generateJsonTable(*ir);
    auto decoder = JsonDecodeAdapter{
        jsonTable,
    };

    auto hello = findFieldByName(jsonTable, "hello");
    auto subHello1 = findFieldByName(jsonTable, "subhello1");
    auto subHello2 = findFieldByName(jsonTable, "subhello2");
    auto subHello3 = findFieldByName(jsonTable, "subhello3");
    auto hello2 = findFieldByName(jsonTable, "hello2");
    REQUIRE(hello.has_value());
    REQUIRE(subHello1.has_value());
    REQUIRE(subHello2.has_value());
    REQUIRE(subHello3.has_value());
    REQUIRE(hello2.has_value());

    decoder.msgBegin(0);
    decoder.fieldBegin(*hello);
    decoder.oneofEnter(0);
    decoder.oneofIndex(0, 2);
    decoder.oneofEnterArm(0, 2);
    decoder.u64(0, 102);
    decoder.oneofExitArm();
    decoder.oneofExit();
    decoder.fieldEnd();
    decoder.fieldBegin(*hello2);
    decoder.arrayPrepare(3);
    decoder.arrayEnterElem(0);
    decoder.u64(0, 10);
    decoder.arrayExitElem();
    decoder.arrayEnterElem(1);
    decoder.u64(0, 11);
    decoder.arrayExitElem();
    decoder.arrayEnterElem(2);
    decoder.u64(0, 12);
    decoder.arrayExitElem();
    decoder.fieldEnd();
    decoder.msgEnd();

    REQUIRE(decoder.ok());
    REQUIRE(decoder.root() ==
            nlohmann::json::object({
                {
                    "hello",
                    nlohmann::json::object({
                        {"case", 789},
                        {"value", 102},
                    }),
                },
                {"hello2", nlohmann::json::array({10, 11, 12})},

            }));

    auto rootObj = decoder.root();
    auto encoder = JsonEncodeAdapter{
        jsonTable,
        rootObj,
    };

    encoder.msgBegin(0);
    REQUIRE(encoder.ok());
    encoder.fieldBegin(*hello);
    REQUIRE(encoder.ok());
    REQUIRE(encoder.oneofIndex(0) == 2);
    encoder.oneofEnterArm(0, 2);
    REQUIRE(encoder.ok());
    REQUIRE(encoder.u64(0) == 102);
    encoder.oneofExitArm();
    REQUIRE(encoder.ok());
    encoder.fieldEnd();
    REQUIRE(encoder.ok());
    encoder.fieldBegin(*hello2);
    REQUIRE(encoder.ok());
    REQUIRE(encoder.arrayLen() == 3);
    encoder.arrayEnterElem(0);
    REQUIRE(encoder.ok());
    REQUIRE(encoder.u64(0) == 10);
    encoder.arrayExitElem();
    REQUIRE(encoder.ok());
    encoder.arrayEnterElem(1);
    REQUIRE(encoder.ok());
    REQUIRE(encoder.u64(0) == 11);
    encoder.arrayExitElem();
    REQUIRE(encoder.ok());
    encoder.arrayEnterElem(2);
    REQUIRE(encoder.ok());
    REQUIRE(encoder.u64(0) == 12);
    encoder.arrayExitElem();
    REQUIRE(encoder.ok());
    encoder.fieldEnd();
    REQUIRE(encoder.ok());
    encoder.msgEnd();
    REQUIRE(encoder.ok());
}
