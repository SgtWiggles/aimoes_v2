#include <catch2/catch_all.hpp>

#include "Helpers.h"
#include "ao/schema/JSONBackend.h"

using namespace ao::schema::json;

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

    REQUIRE(encoder.readI64() == 13);
    REQUIRE(encoder.ok());

    encoder.fieldEnd();
    REQUIRE(encoder.ok());

    encoder.fieldBegin(1);
    REQUIRE(encoder.ok());

    auto readVal = encoder.readU64();
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
    decoder.writeI64(13);
    REQUIRE(decoder.ok());
    decoder.fieldEnd();
    REQUIRE(decoder.ok());

    decoder.fieldBegin(1);
    REQUIRE(decoder.ok());
    decoder.writeU64(14);
    REQUIRE(decoder.ok());
    decoder.fieldEnd();
    REQUIRE(decoder.ok());

    decoder.msgEnd();
    REQUIRE(decoder.ok());

    auto decodedRoot = decoder.root();
    REQUIRE(decodedRoot == data);
}
