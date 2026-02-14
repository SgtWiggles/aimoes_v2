#include <catch2/catch_all.hpp>

#include "ao/schema/Parser.h"

#include "AstQueries.h"

using namespace ao::schema::query;

TEST_CASE("Parser passing message tests", "[parse]") {
    std::string errors;
    bool success;

    std::vector<std::string> passingCases = {
        "message 42 name {}",
        "message 5215 name{\t}",
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
        "import \"hello \\\" world\";message object { 1 hello_world_value "
        "int(bits=123)\t\r\n;}",
        "message object { 1 hello_world_value int(bits=123)\t\r\n;}message "
        "object { 1 hello_world_value int(bits=123)\t\r\n;}",
        R"(
default @cpp(namespace="not_ao");
message name 
{    
    default @cpp(namespace="ao");
    1          name string(bits="12341", hello="world") @cpp(namespace="ao");
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
        // --- minimal / whitespace variants ---
        "message X{}",
        "message   X   {   }",
        "message\tX{\n}\n",
        "message 7 X {}",

        // --- package + import combos ---
        "package a; message X {}",
        "package a.b; import \"x\"; message X {}",
        "import \"a\"; import \"b\"; message X {}",

        // --- import string escape coverage ---
        "import \"hello \\\" world\"; message X {}",
        "import \"line1\\nline2\\tend\"; message X {}",
        "import \"slashes \\\\ and quote \\\"\"; message X {}",

        // --- multiple messages in one input ---
        "message A {} message B {}",
        "package a.b.c; message A {} message B {}",
        "import \"x\"; message A {} import \"y\"; message B {}",

        // --- simple fields (type + ';') ---
        "message A { 1 f int; }",
        "message A { 1 f uint; 2 g bool; 3 h string; 4 i bytes; }",
        "message 42 A { 1 f float; 2 g double; }",

        // --- lots of whitespace between tokens ---
        "message A {\n  1\tf\tint\t;\n  2 g  bool ;\n}\n",

        // --- options on scalar types ---
        "message A { 1 f int(bits=0); }",
        "message A { 1 f int(bits=64,); }",  // trailing comma allowed
        "message A { 1 f int(bits=64, signed=true,); }",
        "message A { 1 f string(bits=\"123\", hello=\"world\",); }",

        // --- numeric literal forms in options ---
        "message A { 1 f double(values=0); }",
        "message A { 1 f double(values=-0.0); }",
        "message A { 1 f double(values=123.456e-7); }",
        "message A { 1 f int(default=-972938748923.9802938402938029e+198); }",
        "message A { 1 f uint(default=+12039280923832.23482930483e+23); }",

        // --- string literals with escapes in options ---
        "message A { 1 f string(default=\"asdboaisbdsa\\\"asodifjasodi\"); }",
        "message A { 1 f string(default=\"\\\\path\\\\to\\\\file\"); }",

        // --- generics: array/optional with multiple args ---
        "message A { 1 f array<int>; }",
        "message A { 1 f array<int, string>; }",
        "message A { 1 f optional<int>; }",
        "message A { 1 f optional<int, string>; }",

        // --- generics with type-args that themselves have options ---
        "message A { 1 f array<int, string(abscd=true, sdofisdf=false,)>; }",
        "message A { 1 f optional<int, oneof(values=1,){ 1 x int; }>; }",

        // --- oneof as a field type --
        "message A { 1 f oneof(bits=10) { 1 x int; 2 y uint; }; }",
        "message A { 1 f oneof(values=12323434) { 1 thing int; }; }",
        "message A { 1 f oneof(bits=10, values=1,) { 1 x int(bits=8,); 2 y "
        "uint(bits=16,); }; }",

        // --- nesting oneof inside optional generic arg (like your example) ---
        "message A { 1 f optional<int, oneof(values=1,){ 1 x int; 2 y string; "
        "}>; }",

        // --- “keyword-y” identifiers you already allow (you had `message
        // 1423213 message {}`) ---
        "message message {}",
        "message 1423213 message {}",
        "package a.b.c; message 1423213 message {}",

        // --- large numbers / long identifiers ---
        "message 999999999999999999 Name123_456 {}",
        "message A { 1 _12382904820j0s9dj0e29ujf09j10fj int; }",
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
        "message 42 name",              // missing body
        "message 42 name {",            // missing closing }
        "message 42 name { } }",        // extra }
        "message 42 name { 1 f int; ",  // missing closing }
        "message name 42 { }",          // wrong order

        // --- fields: shape + semicolons ---
        "message 1 A { 1 f int }",     // missing ';'
        "message 1 A { 1 f int; ; }",  // empty statement
        "message 1 A { 1 int; }",      // missing field name
        "message 1 A { f int; }",      // missing field number
        "message 1 A { 1 f ; }",       // missing type
        "message 1 A { 1 ; }",         // missing name + type

        // --- numbers / identifiers (lex/grammar) ---
        "message 1 A { -1 f int; }",           // negative field number
        "message 1 A { 1.2 f int; }",          // non-integer field number
        "message 1 A { 1 9field int; }",       // ident starts with digit
        "message 1 A { 1 hello-world int; }",  // illegal '-' in ident
        "message 1 A { 1 f int(bits=10; }",    // options: missing ')'

        // --- package / import syntax ---
        "package a.b.c message 1 A {}",    // missing ';' after package
        "package a..b.c; message 1 A {}",  // empty package segment
        "package a.b.; message 1 A {}",    // trailing dot

        "import \"x\" message 1 A {}",            // missing ';' after import
        "import \"unterminated; message 1 A {}",  // unterminated string literal

        // --- generics structure (not type resolution) ---
        "message 1 A { 1 f array<>; }",             // empty generic args
        "message 1 A { 1 f array<int; }",           // missing closing '>'
        "message 1 A { 1 f optional<>; }",          // empty generic args
        "message 1 A { 1 f optional<int,,int>; }",  // empty arg between commas

        // --- options list structure (trailing comma allowed, so don't test it)
        // ---
        "message 1 A { 1 f int(bits 10); }",       // missing '='
        "message 1 A { 1 f int(=10); }",           // missing key
        "message 1 A { 1 f int(bits=); }",         // missing value
        "message 1 A { 1 f int(bits=10,,x=1); }",  // double comma

        // --- numeric literal shape ---
        "message 1 A { 1 f double(values=1e+); }",    // invalid exponent
        "message 1 A { 1 f double(values=--1.0); }",  // invalid number

        // --- oneof structure (based on your passing oneof(...) { ... };) ---
        "message 1 A { 1 f oneof(bits=10) { 1 x int; }",    // missing closing
        "message 1 A { 1 f oneof(bits=10) { x int; }; }",   // missing entry
                                                            // number
        "message 1 A { 1 f oneof(bits=10) { 1 x int }; }",  // missing ';'
                                                            // inside oneof
        "message 1 A { 1 f oneof(bits=10) 1 x int; }; }",  // missing braces for
                                                           // oneof entries

        // --- illegal nesting / placement (pure grammar) ---
        "message 1 A { import \"x\"; 1 f int; }",  // import inside message
        "message 1 A { message 2 B {}; }",         // nested message

        // garbage at top-level
        "x"

        // missing semicolon for import/package
        "import \"x\""
        "package a.b.c"

        // import requires a StringLiteral
        "import x;"
        "import 123;"

        // package requires QualifiedSymbol (no leading/trailing dot, no empty
        // segment)
        "package a..b;"
        "package .a.b;"
        "package a.b.;"
        "package ;"

        // extra tokens after a valid file
        "message A {} ???"

        // top-level semicolon is not a FileDecl
        ";"

        // missing name
        "message {}"

        // message name must be unqualified (no dots)
        "message a.b {}"

        // id must be integer token if present
        "message -1 A {}"
        "message 1.2 A {}"

        // missing block
        "message A"
        "message 1 A"

        // stray tokens between name and block
        "message A 123 {}"
        "message A (x=1) {}"  // options are only on types, not message decl

        // message allows optional trailing ';' but not required.
        // This fails because ';' appears before the block:
        "message A ; {}",

        "default @cpp(namespace=\"not_ao\")message name {}"};

    for (auto const& fileContents : cases) {
        success = ao::schema::parseMatch(fileContents, &errors);
        INFO(fileContents);
        REQUIRE(!success);
    }
}

TEST_CASE("Parsing AST package decls", "[parse]") {
    std::vector<std::pair<std::string, std::vector<std::string>>> packageDecls =
        {
            {"package a.b;", {"a", "b"}},
            {"package a;", {"a"}},
            {"package a.   b.    c;", {"a", "b", "c"}},
            {"package a.   \tb\n.    c;", {"a", "b", "c"}},
            {"\n\r\npackage\n\ra.   \tb\n.    c;", {"a", "b", "c"}},
            {"message 53 A {}\n\r\npackage\n\ra.   \tb\n.    c;",
             {"a", "b", "c"}},
            {"message 53 A {};\n\r\npackage\n\ra.   \tb\n.    c;",
             {"a", "b", "c"}},
        };

    for (auto const& pkg : packageDecls) {
        std::string errs;
        auto ast = ao::schema::parseToAst("file.aosl", pkg.first, &errs);
        INFO(errs);
        REQUIRE(ast != nullptr);
        REQUIRE(ao::schema::query::hasPackageDecl(*ast, {pkg.second}));
    }

    std::vector<std::string> packageDeclsFailure = {
        "message 53 A {}\n\r\n",
        "message 53 A {};\n\r",
    };
    for (auto const& pkg : packageDeclsFailure) {
        std::string errs;
        auto ast = ao::schema::parseToAst("file.aosl", pkg, &errs);
        REQUIRE(ast);
        REQUIRE(!ao::schema::query::hasPackageDecl(*ast));
    }
}

struct MessageDeclTestCase {
    std::string file;
    std::string messageName;
    std::optional<uint64_t> messageNumber;
};

TEST_CASE("Parsing AST message decls", "[parse]") {
    std::vector<MessageDeclTestCase> decls = {
        {"message 53 A {}\n\r\npackage\n\ra.   \tb\n.    c;", "A", 53},
        {"message 53 A {};\n\r\npackage\n\ra.   \tb\n.    c;", "A", 53},
        {"message message {}", "message", {}},
        {"message 1423213 message {}", "message", 1423213},
        {"package a.b.c; message 1423213 message {}", "message", 1423213},
    };
    for (auto const& pkg : decls) {
        std::string errs;
        auto ast = ao::schema::parseToAst("file.aosl", pkg.file, &errs);
        INFO(errs);
        REQUIRE(ast != nullptr);
        auto msg = findMessageByUnresolvedName(ast, pkg.messageName);
        REQUIRE(msg != nullptr);
        REQUIRE(msg->messageId == pkg.messageNumber);
    }
}

TEST_CASE("Parsing AST message fields", "[parse]") {
    std::string file = R"(
message 53 A {
    53 field1 uint;
    54 field2 int;
    55 field3 bool;
    56 field4 bytes;
    57 field5 string;
    58 field6 array<int>;
    59 field7 optional<int>;
    60 field8 oneof {
        1 branch1 int;
        2 branch2 bytes;
    };
    61 field9 custom.a;
}

)";
    std::string errs;
    auto ast = ao::schema::parseToAst("file.aosl", file, &errs);
    INFO(errs);
    REQUIRE(ast != nullptr);
    auto msg = findMessageByUnresolvedName(ast, "A");
    REQUIRE(msg != nullptr);
    REQUIRE(msg->messageId == 53);

    using ao::schema::AstBaseType;
    std::vector<std::tuple<std::string, uint64_t, ao::schema::AstBaseType>>
        simpleCases = {
            {"field1", 53, AstBaseType::UINT},
            {"field2", 54, AstBaseType::INT},
            {"field3", 55, AstBaseType::BOOL},
            {"field4", 56, AstBaseType::BYTES},
            {"field5", 57, AstBaseType::STRING},
            {"field6", 58, AstBaseType::ARRAY},
            {"field7", 59, AstBaseType::OPTIONAL},
            {"field8", 60, AstBaseType::ONEOF},
            {"field9", 61, AstBaseType::USER},
        };

    for (auto const& [fieldName, fieldNum, typeBase] : simpleCases) {
        auto field = findFieldByName(*msg, fieldName);
        REQUIRE(field != nullptr);
        REQUIRE(field->fieldNumber == fieldNum);
        REQUIRE(field->typeName.type == typeBase);
    }

    {
        auto arrayField = findFieldByName(*msg, "field6");
        REQUIRE(arrayField != nullptr);
        REQUIRE(arrayField->typeName.subtypes.size() == 1);
        auto subtype = arrayField->typeName.subtypes[0];
        REQUIRE(subtype != nullptr);
        REQUIRE(subtype->type == AstBaseType::INT);
    }
    {
        auto optionalField = findFieldByName(*msg, "field7");
        REQUIRE(optionalField != nullptr);
        REQUIRE(optionalField->typeName.subtypes.size() == 1);
        auto subtype = optionalField->typeName.subtypes[0];
        REQUIRE(subtype != nullptr);
        REQUIRE(subtype->type == AstBaseType::INT);
    }
    {
        auto oneofField = findFieldByName(*msg, "field8");
        REQUIRE(oneofField != nullptr);
        REQUIRE(oneofField->typeName.block.fields.size() == 2);
    }
    {
        auto customField = findFieldByName(*msg, "field9");
        REQUIRE(customField != nullptr);
        REQUIRE((customField->typeName.name ==
                 ao::schema::AstQualifiedName{{"custom", "a"}}));
    }
}
