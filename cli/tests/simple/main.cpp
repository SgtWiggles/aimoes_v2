#include <catch2/catch_all.hpp>

#include <span>

#include <ao/schema/CodecCommon.h>
#include <ao/schema/DiskCodec.h>
#include <ao/schema/NetCodec.h>

#include "simple/AoslCppSimple_messages.h"

#include "../CppTestHelpers.h"

char const baseIr[] =
#include "simple/AoslCppSimple_messages.aoir.h"
    ;
constexpr size_t baseIrSize = sizeof(baseIr);
auto const irSpan = std::span{(std::byte const*)baseIr, baseIrSize};

// This function is here to ensure add is generated in TestMessage5
namespace messages {
messages::TestMessage5 TestMessage5::add(messages::TestMessage5 const& other) {
    return {
        .value1 = other.value1 + value1,
        .value2 = other.value2 + value2,
        .value3 = other.value3.value_or(0) + value3.value_or(0),
    };
}
}  // namespace messages

TEMPLATE_LIST_TEST_CASE("Simple message round trip 1",
                        "[simple]",
                        StreamTypes) {
    messages::TestMessage input{
        .value = 1234567890123456789,
    };

    messages::TestMessage output;

    using WS = typename TestType::WS;
    using RS = typename TestType::RS;
    cppRoundTrip<WS, RS>(irSpan, input, output);

    REQUIRE(input.value == output.value);
}

TEMPLATE_LIST_TEST_CASE("Simple message 2 round trip 1",
                        "[simple]",
                        StreamTypes) {
    messages::TestMessage2 input{
        .value = 1234567890123456789,
    };

    messages::TestMessage2 output;

    using WS = typename TestType::WS;
    using RS = typename TestType::RS;
    cppRoundTrip<WS, RS>(irSpan, input, output);

    REQUIRE(input.value == output.value);
}

TEMPLATE_LIST_TEST_CASE("Simple message 3 round trip 1",
                        "[simple]",
                        StreamTypes) {
    messages::TestMessage3 input{
        .value1 = 1234567890123456789,
        .value2 = -1234567890123456789,
    };

    messages::TestMessage3 output;

    using WS = typename TestType::WS;
    using RS = typename TestType::RS;
    cppRoundTrip<WS, RS>(irSpan, input, output);

    REQUIRE(input.value1 == output.value1);
    REQUIRE(input.value2 == output.value2);
}

TEMPLATE_LIST_TEST_CASE("Simple message 4 round trip 1",
                        "[simple]",
                        StreamTypes) {
    messages::TestMessage4 input{
        .value1 = 1234567890123456789,
        .value2 = -1234567890123456789,
    };

    messages::TestMessage4 output;

    using WS = typename TestType::WS;
    using RS = typename TestType::RS;
    cppRoundTrip<WS, RS>(irSpan, input, output);

    REQUIRE(input == output);
}

TEMPLATE_LIST_TEST_CASE("Simple message 5 round trip 1",
                        "[simple]",
                        StreamTypes) {
    messages::TestMessage5 v0{1, 2, 3};
    messages::TestMessage5 v1{1, 2, {}};
    auto added = v0.add(v1);
    REQUIRE(added.value1 == 2);
    REQUIRE(added.value2 == 4);
    REQUIRE(added.value3 == 3);
}
