#include <ao/schema/Parser.h>

#include <lexy/action/parse.hpp>
#include <lexy/callback/string.hpp>
#include <lexy/dsl.hpp>
#include <lexy/input/string_input.hpp>
#include <lexy/input_location.hpp>
#include <lexy_ext/report_error.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ao/schema/Ast.h"  // your AST definitions

using namespace ao::schema;
namespace grammar {

namespace dsl = lexy::dsl;
auto constexpr identifier = dsl::identifier(dsl::ascii::alpha_underscore,
                                            dsl::ascii::alpha_digit_underscore);

struct WS {
    static constexpr auto rule = dsl::ascii::space | dsl::ascii::newline |
                                 LEXY_LIT("//") >> dsl::until(LEXY_LIT("\n")) |
                                 LEXY_LIT("/*") >> dsl::until(LEXY_LIT("*/"));
    static constexpr auto value = lexy::noop;
};

// JSON string literal from example
struct StringLiteral : lexy::token_production {
    struct invalid_char {
        static LEXY_CONSTEVAL auto name() {
            return "invalid character in string literal";
        }
    };

    // A mapping of the simple escape sequences to their replacement values.
    static constexpr auto escaped_symbols = lexy::symbol_table<char>  //
                                                .map<'"'>('"')
                                                .map<'\\'>('\\')
                                                .map<'/'>('/')
                                                .map<'b'>('\b')
                                                .map<'f'>('\f')
                                                .map<'n'>('\n')
                                                .map<'r'>('\r')
                                                .map<'t'>('\t');

    // In JSON, a Unicode code point can be specified by its encoding in UTF-16:
    // * code points <= 0xFFFF are specified using `\uNNNN`.
    // * other code points are specified by two surrogate UTF-16 sequences.
    // However, we don't combine the two surrogates into the final code point,
    // instead keep them separate and require a later pass that merges them if
    // necessary. (This behavior is allowed by the standard).
    struct code_point_id {
        // We parse the integer value of a UTF-16 code unit.
        static constexpr auto rule =
            LEXY_LIT("u") >> dsl::code_unit_id<lexy::utf16_encoding, 4>;
        // And convert it into a code point, which might be a surrogate.
        static constexpr auto value = lexy::construct<lexy::code_point>;
    };

    static constexpr auto rule = [] {
        // Everything is allowed inside a string except for control characters.
        auto code_point = (-dsl::unicode::control).error<invalid_char>;

        // Escape sequences start with a backlash and either map one of the
        // symbols, or a Unicode code point.
        auto escape = dsl::backslash_escape.symbol<escaped_symbols>().rule(
            dsl::p<code_point_id>);

        // String of code_point with specified escape sequences, surrounded by
        // ". We abort string parsing if we see a newline to handle missing
        // closing ".
        return dsl::quoted.limit(dsl::ascii::newline)(code_point, escape);
    }();

    static constexpr auto value =
        lexy::as_string<std::string, lexy::utf8_encoding>;
};

struct NumberLiteral : lexy::token_production {
    static constexpr auto exponentPart = dsl::if_(
        (LEXY_LIT("e") / LEXY_LIT("E")) >> dsl::sign + dsl::integer<uint64_t>);
    static constexpr auto fractionalPart =
        dsl::if_(dsl::period >> dsl::integer<uint64_t> + exponentPart);
    static constexpr auto rule =
        dsl::peek(LEXY_LIT("+") / LEXY_LIT("-") / dsl::ascii::digit) >>
        dsl::sign + dsl::integer<uint64_t> + fractionalPart;
};

struct ValueLiteral {
    static constexpr auto rule = dsl::p<NumberLiteral> | dsl::p<StringLiteral> |
                                 LEXY_LIT("true") | LEXY_LIT("false");
};

struct DirectiveKeyValuePair {
    static constexpr auto rule = LEXY_LIT("=") + dsl::p<ValueLiteral>;
};

struct DirectiveSet {
    /*
    static constexpr auto rule =
        LEXY_LIT("@") >>
        identifier +
            dsl::round_bracketed(dsl::list(
                identifier >> dsl::p<DirectiveKeyValuePair>,
                dsl::trailing_sep(",")));
                */
    static constexpr auto rule = dsl::whitespace(dsl::ascii::space);
};
struct UnqualifiedSymbol {
    static constexpr auto rule =
        dsl::identifier(dsl::ascii::alpha_underscore,
                        dsl::ascii::alpha_digit_underscore);
};
struct QualifiedSymbol {
    static constexpr auto rule =
        dsl::list(UnqualifiedSymbol::rule, dsl::sep(LEXY_LIT(".")));
};

struct TypeName {
    static constexpr auto rule =
        LEXY_LIT("bool") | LEXY_LIT("int") | LEXY_LIT("uint") |
        LEXY_LIT("float") | LEXY_LIT("double") | LEXY_LIT("string") |
        LEXY_LIT("bytes") | LEXY_LIT("array") | LEXY_LIT("optional") |
        LEXY_LIT("oneof") | dsl::p<QualifiedSymbol>;
};

struct TypeArgs {
    static constexpr auto rule = dsl::opt(dsl::angle_bracketed(dsl::list(
        dsl::peek(dsl::p<TypeName>) >> dsl::recurse_branch<struct Type>,
        dsl::trailing_sep(LEXY_LIT(",")))));
};

struct TypePropertyArgItem {
    static constexpr auto rule = identifier >>
                                 LEXY_LIT("=") + dsl::p<ValueLiteral>;
};

struct TypeProperties {
    static constexpr auto rule = dsl::opt(dsl::round_bracketed(
        dsl::list(dsl::peek(identifier) >> dsl::p<TypePropertyArgItem>,
                  dsl::trailing_sep(LEXY_LIT(",")))));
};

struct TypeMessageBlock {
    static constexpr auto rule =
        dsl::opt(dsl::peek(LEXY_LIT("{")) >> dsl::recurse<struct MessageBlock>);
};

struct Type {
    static constexpr auto rule =
        dsl::p<TypeName> >>
        dsl::p<TypeArgs> + dsl::p<TypeProperties> + dsl::p<TypeMessageBlock>;
};

struct FieldDef {
    static constexpr auto rule = LEXY_LIT("default") >> dsl::p<DirectiveSet> |
                                 dsl::integer<unsigned int, dsl::decimal> >>
                                     dsl::p<UnqualifiedSymbol> + dsl::p<Type>;
};

struct MessageBlock {
    static constexpr auto rule =
        dsl::curly_bracketed(dsl::while_(dsl::p<FieldDef> >> LEXY_LIT(";")));
};

struct MessageDecl {
    static constexpr auto rule =
        LEXY_LIT("message") >>
        dsl::opt(dsl::integer<unsigned int, dsl::decimal>) +
            dsl::p<UnqualifiedSymbol> + dsl::p<MessageBlock> +
            dsl::opt(LEXY_LIT(";"));
};

struct ImportDecl {
    static constexpr auto rule = LEXY_LIT("import") >>
                                 dsl::p<StringLiteral> + LEXY_LIT(";");
};

struct PackageDecl {
    static constexpr auto rule = LEXY_LIT("package") >>
                                 dsl::p<QualifiedSymbol> + LEXY_LIT(";");
};

struct FileDecl {
    static constexpr auto rule =
        dsl::p<MessageDecl> | dsl::p<ImportDecl> | dsl::p<PackageDecl>;
};

struct File {
    static constexpr auto whitespace = WS::rule;
    static constexpr auto rule = dsl::while_(dsl::p<FileDecl>) + dsl::eof;
    static constexpr auto value = lexy::forward<uint64_t>;
};
}  // namespace grammar

namespace ao::schema {
struct StringStreamOutputIterator {
    auto operator*() const noexcept { return *this; }
    auto operator++(int) const noexcept { return *this; }

    StringStreamOutputIterator& operator=(char c) {
        (*ss) << c;
        return *this;
    }
    std::stringstream* ss;
};

bool parseMatch(std::string_view str, std::string* errsOut) {
    auto input = lexy::string_input<lexy::utf8_encoding>{str};
    std::stringstream ss;
    auto reporter = lexy_ext::_report_error<StringStreamOutputIterator>{
        ._iter = StringStreamOutputIterator{&ss},
    };
    auto res = lexy::validate<grammar::File>(input, reporter);
    if (errsOut) {
        *errsOut = ss.str();
    }
    return res.error_count() == 0;
}
}  // namespace ao::schema
