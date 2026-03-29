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
    std::vector<int64_t> testValues = {
        0,
        -1,
        1,
        9223372036854775807LL,
        -9223372036854775807LL - 1,
        9223372036854775806LL,
        -9223372036854775807LL,

        127,
        128,
        -127,
        -128,
        255,
        256,
        -255,
        -256,
        16383,
        16384,
        -16383,
        -16384,
        1048575,
        1048576,
        -1048575,
        -1048576,
        2097151,
        2097152,
        -2097151,
        -2097152,
        268435455,
        268435456,
        -268435455,
        -268435456,
        34359738367LL,
        34359738368LL,
        -34359738367LL,
        -34359738368LL,
        4398046511103LL,
        4398046511104LL,
        -4398046511103LL,
        -4398046511104LL,
        562949953421311LL,
        562949953421312LL,
        -562949953421311LL,
        -562949953421312LL,
        72057594037927935LL,
        72057594037927936LL,
        -72057594037927935LL,
        -72057594037927936LL,

        4611686018427387903LL,
        4611686018427387904LL,
        -4611686018427387903LL,
        -4611686018427387904LL,

        -2,
        2,
        -3,
        3,

        6148914691236517205LL,
        -6148914691236517206LL,
    };

    for (auto v : testValues) {
        messages::TestMessage input{.value = v};
        messages::TestMessage output{};

        using WS = typename TestType::WS;
        using RS = typename TestType::RS;
        cppRoundTrip<WS, RS>(irSpan, input, output);

        REQUIRE(input.value == output.value);
    }
}
