#include <catch2/catch_all.hpp>

#include "ao/schema/Parser.h"

TEST_CASE("Parser simple message test", "[parse]") {
    std::string errors;
    bool success;

    std::vector<std::string> passingCases = {
        "message 42 name {}", "message 5215 name{\t}",
        R"(message name 
{    
    1          name string;
    12         name bool;
    123        name int;
    1234       name uint;
    12345      name float;
    123456     name double;
    1234567    name string;
    12345678   name bytes;
    123456789  name array<int>;
    1234567890 name optional<int>;
}
)",
        "message 1234332423 name { 1 _12382904820j0s9dj0e29ujf09j10fj "
        "oneof(bits=10) { 1231242 name int; }; }",
        "message object {}",
        "import \"hello \\\" world\";message object { 1 hello_world_value int(bits=123)\t\r\n;}",
        "message object { 1 hello_world_value int(bits=123)\t\r\n;}message object { 1 hello_world_value int(bits=123)\t\r\n;}",
R"(message name 
{    
    1          name string(bits="12341", hello="world");
    12         name bool(values=123.123e1232);
    123        name int(default=-972938748923.9802938402938029e+19820234982734);
    1234       name uint;
    12345      name float;
    123456     name double;
    1234567    name string;
    12345678   name bytes;
    123456789  name array<int, string(abscd=true)>;
    1234567890 name optional<int>;
}
)"
        "message 1423213 message {}"
    };

    for (auto const& fileContents : passingCases) {
        success = ao::schema::parseMatch(fileContents, &errors);
        INFO(fileContents);
        INFO(errors);
        REQUIRE(success);
    }
}