#include <catch2/catch_all.hpp>

#include <iostream>

#include "Helpers.h"
#include "ao/schema/CppBackend.h"

using namespace ao::schema;

TEST_CASE("Basic cpp test", "[cpp]") {
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

    ao::schema::ErrorContext errs;
    std::stringstream header;
    std::stringstream irOut;
    auto outFiles = cpp::OutputFiles{
        .header = header,
        .ir = irOut,
    };
    auto success = cpp::generateCppCode(*ir, errs, outFiles);
    REQUIRE(success);
}
