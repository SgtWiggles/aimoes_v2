#include <catch2/catch_all.hpp>

#include <bit>

#include "Helpers.h"
#include "ao/schema/JSONBackend.h"
#include "ao/schema/VM.h"

using namespace ao::schema::json;
using namespace ao::schema::vm;

TEST_CASE("Json encode 1", "[json][codec]") {
    std::string buildErrors;
    auto ir = buildToIR(R"(
package pkg;
message 100 Test {
    10 hello int(bits=10);
    12 world uint(bits=10);
})",
                        buildErrors);
    INFO(buildErrors);
    REQUIRE(ir.has_value());

    ao::schema::ErrorContext errs;
    auto encodeState = generateJsonEncodeState(*ir, errs);
    INFO(errs.toString());
    REQUIRE(errs.ok());

    std::vector<std::byte> data;
    data.resize(1024);
    ao::pack::bit::WriteStream ws{data};
    auto msgNumber = encodeState.prog.messageId(100);
    REQUIRE(msgNumber);
    auto encoded = encodeJson(encodeState, nlohmann::json::parse(R"({
    "hello": -129,
    "world": 129
})"),
                              ws, *msgNumber);
    REQUIRE(encoded);

    REQUIRE(ws.bitSize() == 20);
    REQUIRE(ws.byteSize() == 3);

    ao::pack::bit::ReadStream rs{{data.data(), ws.byteSize()}};
    uint64_t read = 0;
    rs.bits(read, 10);
    REQUIRE(rs.ok());
    REQUIRE(read == (-129 & 0x03FF));

    rs.bits(read, 10);
    REQUIRE(rs.ok());
    REQUIRE(read == (129 & 0x03FF));
}
