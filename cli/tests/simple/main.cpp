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
