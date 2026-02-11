#include <catch2/catch_all.hpp>

#include "ao/schema/Parser.h"

TEST_CASE("Parser passing message tests", "[parse]") {
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
    1234       name uint(default=+12039280923832.23482930483e+23234);
    12345      name float(default="asdboaisbdsa\"asodifjasodi");
    123456     name double;
    1234567    name string;
    12345678   name bytes;
    123456789  name array<int, string(abscd=true, sdofisdf=false)>;
    123456789  name array<int, string(abscd=true, sdofisdf=false,),>;
    1234567890 name optional<int, oneof(values=12323434){
        1 thing int;
    }>;
}
)"
        "message 1423213 message {}",
        "package a.b.c;message 1423213 message {}",
        "package a.b.c;message 1423213 message {}package b.c.a.c;",
    };

    for (auto const& fileContents : passingCases) {
        success = ao::schema::parseMatch(fileContents, &errors);
        INFO(fileContents);
        INFO(errors);
        REQUIRE(success);
    }
}
TEST_CASE("Parser failing message tests", "[parse]") {
    std::string errors;
    bool success;

std::vector<std::string> cases = {
    //clang-format off
    // --- message header / braces ---
    "message 42 name",                          // missing body
    "message 42 name {",                        // missing closing }
    "message 42 name { } }",                    // extra }
    "message 42 name { 1 f int; ",              // missing closing }
    "message name 42 { }",                      // wrong order

    // --- fields: shape + semicolons ---
    "message 1 A { 1 f int }",                  // missing ';'
    "message 1 A { 1 f int; ; }",               // empty statement
    "message 1 A { 1 int; }",                   // missing field name
    "message 1 A { f int; }",                   // missing field number
    "message 1 A { 1 f ; }",                    // missing type
    "message 1 A { 1 ; }",                      // missing name + type

    // --- numbers / identifiers (lex/grammar) ---
    "message 1 A { -1 f int; }",                // negative field number
    "message 1 A { 1.2 f int; }",               // non-integer field number
    "message 1 A { 1 9field int; }",            // ident starts with digit
    "message 1 A { 1 hello-world int; }",       // illegal '-' in ident
    "message 1 A { 1 f int(bits=10; }",         // options: missing ')'

    // --- package / import syntax ---
    "package a.b.c message 1 A {}",             // missing ';' after package
    "package a..b.c; message 1 A {}",           // empty package segment
    "package a.b.; message 1 A {}",             // trailing dot

    "import \"x\" message 1 A {}",              // missing ';' after import
    "import \"unterminated; message 1 A {}",    // unterminated string literal

    // --- generics structure (not type resolution) ---
    "message 1 A { 1 f array<>; }",             // empty generic args
    "message 1 A { 1 f array<int; }",           // missing closing '>'
    "message 1 A { 1 f optional<>; }",          // empty generic args
    "message 1 A { 1 f optional<int,,int>; }",  // empty arg between commas

    // --- options list structure (trailing comma allowed, so don't test it) ---
    "message 1 A { 1 f int(bits 10); }",        // missing '='
    "message 1 A { 1 f int(=10); }",            // missing key
    "message 1 A { 1 f int(bits=); }",          // missing value
    "message 1 A { 1 f int(bits=10,,x=1); }",   // double comma 

    // --- numeric literal shape ---
    "message 1 A { 1 f double(values=1e+); }",  // invalid exponent
    "message 1 A { 1 f double(values=--1.0); }",// invalid number

    // --- oneof structure (based on your passing oneof(...) { ... };) ---
    "message 1 A { 1 f oneof(bits=10) { 1 x int; }",    // missing closing message + maybe missing ';'
    "message 1 A { 1 f oneof(bits=10) { x int; }; }",   // missing entry number
    "message 1 A { 1 f oneof(bits=10) { 1 x int }; }",  // missing ';' inside oneof
    "message 1 A { 1 f oneof(bits=10) 1 x int; }; }",   // missing braces for oneof entries

    // --- illegal nesting / placement (pure grammar) ---
    "message 1 A { import \"x\"; 1 f int; }",   // import inside message
    "message 1 A { message 2 B {}; }",          // nested message
    //clang-format on
};

    for (auto const& fileContents : cases) {
        success = ao::schema::parseMatch(fileContents, &errors);
        INFO(fileContents);
        REQUIRE(!success);
    }
}
