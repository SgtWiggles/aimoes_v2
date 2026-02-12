#include <ao/schema/Parser.h>

#include <lexy/action/parse.hpp>
#include <lexy/callback.hpp>
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

using namespace ao::schema;

namespace grammar {
struct ParsingContext {
    std::string_view filepath;
    lexy::string_input<lexy::utf8_encoding> const* input;
    SourceLocation getSourceLocation(
        lexy::input_reader<lexy::string_input<lexy::utf8_encoding>>::iterator
            pos) const {
        auto loc = lexy::get_input_location(*input, pos);
        return SourceLocation{
            .file = std::string{filepath},
            .line = loc.line_nr(),
            .col = loc.column_nr(),
        };
    }
};

namespace dsl = lexy::dsl;
auto constexpr identifier = dsl::identifier(dsl::ascii::alpha_underscore,
                                            dsl::ascii::alpha_digit_underscore);

struct Identifier {
    static constexpr auto rule = identifier;
    static constexpr auto value = lexy::as_string<std::string>;
};

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
    struct IntegerPart {
        static constexpr auto rule = dsl::sign + dsl::integer<int64_t>;
        static constexpr auto value = lexy::as_integer<int64_t>;
    };
    struct ExponentPart {
        static constexpr auto rule =
            dsl::opt((LEXY_LIT("e") / LEXY_LIT("E")) >> dsl::p<IntegerPart>);
        static constexpr auto value = lexy::forward<std::optional<int64_t>>;
    };
    struct FracPart {
        static constexpr auto rule =
            dsl::opt(dsl::period >> dsl::integer<uint64_t>);
        static constexpr auto value = lexy::forward<std::optional<uint64_t>>;
    };

    static constexpr auto rule =
        dsl::peek(LEXY_LIT("+") / LEXY_LIT("-") / dsl::ascii::digit) >>
        dsl::p<IntegerPart> + dsl::p<FracPart> + dsl::p<ExponentPart>;
    static constexpr auto value = lexy::callback_with_state<AstValueLiteral>(
        [](ParsingContext const& ctx,
           int64_t value,
           std::optional<uint64_t> frac,
           std::optional<int64_t> exp) {
            if (!frac || !exp) {
                return AstValueLiteral{
                    .type = ValueLiteralType::INT,
                    .contents = std::to_string(value),
                    .loc = {},
                };
            } else {
                return AstValueLiteral{
                    .type = ValueLiteralType::NUMBER,
                    .contents = std::format("{}.{}e{}", value, frac.value_or(0),
                                            exp.value_or(0)),
                    .loc = {},
                };
            }
        });
};

struct BooleanLiteral {
    struct TrueLiteral {
        static constexpr auto rule = LEXY_LIT("true");
        static constexpr auto value = lexy::constant(true);
    };
    struct FalseLiteral {
        static constexpr auto rule = LEXY_LIT("false");
        static constexpr auto value = lexy::constant(false);
    };
    static constexpr auto rule = dsl::p<TrueLiteral> | dsl::p<FalseLiteral>;
    static constexpr auto value = lexy::callback_with_state<AstValueLiteral>(
        [](ParsingContext const& ctx, bool v) {
            return AstValueLiteral{
                .type = ValueLiteralType::BOOLEAN,
                .contents = v ? "true" : "false",
                .loc = {},
            };
        });
};

struct ValueLiteral {
    static constexpr auto rule =
        dsl::p<NumberLiteral> | dsl::p<StringLiteral> | dsl::p<BooleanLiteral>;
    static constexpr auto value = lexy::callback_with_state<AstValueLiteral>(
        [](ParsingContext const& ctx, AstValueLiteral literal) {
            return literal;
        },
        [](ParsingContext const& ctx, std::string str) {
            return AstValueLiteral{
                .type = ValueLiteralType::STRING,
                .contents = str,
                .loc = {},
            };
        });
};

struct DirectiveKeyValuePair {
    static constexpr auto rule = dsl::p<Identifier> >>
                                 LEXY_LIT("=") + dsl::p<ValueLiteral>;
    static constexpr auto value =
        lexy::construct<std::pair<std::string, AstValueLiteral>>;
};

struct DirectiveProfile {
    struct ValueList {
        static constexpr auto rule =
            dsl::list(dsl::p<DirectiveKeyValuePair>,
                      dsl::trailing_sep(LEXY_LIT(",")));
        static constexpr auto value =
            lexy::as_list<std::vector<std::pair<std::string, AstValueLiteral>>>;
    };
    static constexpr auto rule = LEXY_LIT("@") >>
                                 dsl::p<Identifier> +
                                     dsl::round_bracketed(dsl::p<ValueList>);
    static constexpr auto value = lexy::callback_with_state<bool>(
        [](ParsingContext const& ctx,
           std::string identifier,
           std::vector<std::pair<std::string, AstValueLiteral>> directives) {
            return false;
        });
};

struct DirectiveSet {
    struct ProfileList {
        static constexpr auto rule = dsl::list(dsl::p<DirectiveProfile>);
        static constexpr auto value = lexy::as_list<std::vector<bool>>;
    };
    static constexpr auto rule = dsl::opt(dsl::p<ProfileList>);
    static constexpr auto value = lexy::callback_with_state<bool>(
        [](ParsingContext const& ctx, std::optional<std::vector<bool>> values) {
            return false;
        });
};
struct UnqualifiedSymbol {
    static constexpr auto rule =
        dsl::identifier(dsl::ascii::alpha_underscore,
                        dsl::ascii::alpha_digit_underscore);
    static constexpr auto value =
        lexy::as_string<std::string, lexy::utf8_encoding>;
};
struct QualifiedSymbol {
    static constexpr auto rule =
        dsl::list(dsl::p<UnqualifiedSymbol>, dsl::sep(LEXY_LIT(".")));

    static constexpr auto value = lexy::as_list<std::vector<std::string>>;
};

struct TypeName {
#define TYPE_CASE(NAME, STR)                                             \
    struct NAME {                                                        \
        static constexpr auto rule = LEXY_LIT(STR);                      \
        static constexpr auto value = lexy::constant(AstBaseType::NAME); \
    }
    TYPE_CASE(BOOL, "bool");
    TYPE_CASE(INT, "int");
    TYPE_CASE(UINT, "uint");
    TYPE_CASE(F32, "float");
    TYPE_CASE(F64, "double");
    TYPE_CASE(STRING, "string");
    TYPE_CASE(BYTES, "bytes");
    TYPE_CASE(ARRAY, "array");
    TYPE_CASE(OPTIONAL, "optional");
    TYPE_CASE(ONEOF, "oneof");

#undef TYPE_CASE

    struct USER {
        static constexpr auto rule = dsl::p<QualifiedSymbol>;
        static constexpr auto value = lexy::forward<std::vector<std::string>>;
    };

    static constexpr auto rule =
        dsl::p<BOOL> | dsl::p<INT> | dsl::p<UINT> | dsl::p<F32> | dsl::p<F64> |
        dsl::p<STRING> | dsl::p<BYTES> | dsl::p<ARRAY> | dsl::p<OPTIONAL> |
        dsl::p<ONEOF> | dsl::p<USER>;
    static constexpr auto value =
        lexy::callback_with_state<std::pair<AstBaseType, AstQualifiedName>>(
            [](ParsingContext const& ctx, AstBaseType baseType) {
                return std::pair<AstBaseType, AstQualifiedName>{
                    baseType,
                    {},
                };
            },
            [](ParsingContext const& ctx, std::vector<std::string> baseType) {
                return std::pair<AstBaseType, AstQualifiedName>{
                    AstBaseType::USER,
                    std::move(baseType),
                };
            });
};

struct TypeArgs {
    struct TypeArg {
        static constexpr auto rule =
            dsl::peek(dsl::p<TypeName>) >> dsl::recurse_branch<struct Type>;
        static constexpr auto value =
            lexy::callback_with_state<std::shared_ptr<AstType>>(
                [](ParsingContext const& ctx, AstType type) {
                    return std::make_shared<AstType>(std::move(type));
                });
    };
    struct TypeList {
        static constexpr auto rule =
            dsl::list(dsl::p<TypeArg>, dsl::trailing_sep(LEXY_LIT(",")));
        static constexpr auto value =
            lexy::as_list<std::vector<std::shared_ptr<AstType>>>;
    };
    static constexpr auto rule =
        dsl::opt(dsl::angle_bracketed(dsl::p<TypeList>));
    static constexpr auto value =
        lexy::callback_with_state<std::vector<std::shared_ptr<AstType>>>(
            [](ParsingContext const& ctx,
               std::optional<std::vector<std::shared_ptr<AstType>>> v) {
                return v.value_or({});
            });
};

struct TypeProperties {
    struct ArgItem {
        static constexpr auto rule = dsl::p<Identifier> >>
                                     LEXY_LIT("=") + dsl::p<ValueLiteral>;
        static constexpr auto value =
            lexy::callback_with_state<AstTypeProperty>(
                [](ParsingContext const& ctx,
                   std::string ident,
                   AstValueLiteral value) {
                    return AstTypeProperty{
                        .name = std::move(ident),
                        .value = std::move(value),
                        .loc = {},  // TODO patch in source location once we
                                    // figure this out
                    };
                });
    };
    struct ArgList {
        static constexpr auto rule =
            dsl::list(dsl::peek(identifier) >> dsl::p<ArgItem>,
                      dsl::trailing_sep(LEXY_LIT(",")));
        static constexpr auto value =
            lexy::as_list<std::vector<AstTypeProperty>>;
    };
    static constexpr auto rule =
        dsl::opt(dsl::round_bracketed(dsl::p<ArgList>));
    static constexpr auto value = lexy::callback_with_state<AstTypeProperties>(
        [](ParsingContext const& ctx,
           std::optional<std::vector<AstTypeProperty>> props) {
            return AstTypeProperties{props.value_or({})};
        });
};

struct TypeMessageBlock {
    static constexpr auto rule =
        dsl::opt(dsl::peek(LEXY_LIT("{")) >> dsl::recurse<struct MessageBlock>);
    static constexpr auto value = lexy::callback_with_state<AstMessageBlock>(
        [](ParsingContext const& ctx, std::optional<AstMessageBlock> blk) {
            return blk.value_or({});
        });
};

struct Type {
    static constexpr auto rule =
        dsl::p<TypeName> >>
        dsl::p<TypeArgs> + dsl::p<TypeProperties> + dsl::p<TypeMessageBlock>;

    static constexpr auto value = lexy::callback_with_state<AstType>(
        [](ParsingContext const& ctx,
           std::pair<AstBaseType, AstQualifiedName> typeName,
           std::vector<std::shared_ptr<AstType>> typeArgs,
           AstTypeProperties typeProps,
           auto oneOfMessage) {
            return AstType{
                .type = typeName.first,
                .name = typeName.second,
                .subtypes = std::move(typeArgs),
                .properties = std::move(typeProps),
                .block = {},  // TODO once we get the other block done do this
                .loc = {},    // TODO figure this out later
            };
        });
};

struct DefaultDecl {
    static constexpr auto rule = LEXY_LIT("default") >> dsl::p<DirectiveSet>;
    static constexpr auto value = lexy::callback_with_state<AstDecl>(
        [](ParsingContext const& ctx, auto directiveSet) { return AstDecl{}; });
};

struct FieldDef {
    struct Field {
        static constexpr auto rule =
            dsl::integer<uint64_t, dsl::decimal> >>
            dsl::p<UnqualifiedSymbol> + dsl::p<Type> + dsl::p<DirectiveSet>;
        static constexpr auto value = lexy::callback_with_state<bool>(
            [](ParsingContext const& ctx,
               auto fieldNum,
               auto name,
               auto type,
               auto directiveSet) { return true; });
    };
    static constexpr auto rule =
        dsl::p<DefaultDecl> >> LEXY_LIT(";") | dsl::p<Field> >> LEXY_LIT(";");
    static constexpr auto value = lexy::callback_with_state<AstFieldDecl>(
        [](ParsingContext const& ctx, auto field) { return AstFieldDecl{}; });
};

struct MessageBlock {
    struct DefList {
        static constexpr auto rule = dsl::list(dsl::p<FieldDef>);
        static constexpr auto value = lexy::as_list<std::vector<AstFieldDecl>>;
    };
    static constexpr auto rule =
        dsl::curly_bracketed(dsl::opt(dsl::p<DefList>));
    static constexpr auto value = lexy::callback_with_state<AstMessageBlock>(
        [](ParsingContext const& ctx,
           std::optional<std::vector<AstFieldDecl>> decls) {
            return AstMessageBlock{
                .fields = decls.value_or({}),
                .loc = {},  // TODO patch in location
            };
        });
};

struct MessageDecl {
    static constexpr auto rule =
        LEXY_LIT("message") >>
        dsl::opt(dsl::integer<uint64_t>) + dsl::p<UnqualifiedSymbol> +
            dsl::p<MessageBlock> + dsl::opt(LEXY_LIT(";"));
    static constexpr auto value = lexy::callback_with_state<AstDecl>(
        [](ParsingContext const& ctx,
           std::optional<uint64_t> msgNumber,
           std::string name,
           AstMessageBlock block,
           lexy::nullopt) { return AstDecl{}; },
        [](ParsingContext const& ctx,
           std::optional<uint64_t> msgNumber,
           std::string name,
           AstMessageBlock block) { return AstDecl{}; });
};

struct ImportDecl {
    static constexpr auto rule = LEXY_LIT("import") >>
                                 dsl::p<StringLiteral> + LEXY_LIT(";");
    static constexpr auto value = lexy::callback_with_state<AstDecl>(
        [](ParsingContext const& ctx, std::string path) { return AstDecl{}; });
};

struct PackageDecl {
    static constexpr auto rule = LEXY_LIT("package") >>
                                 dsl::p<QualifiedSymbol> + LEXY_LIT(";");
    static constexpr auto value = lexy::callback_with_state<AstDecl>(
        [](ParsingContext const& ctx, std::vector<std::string> name) {
            auto qualifiedName = AstQualifiedName{name};
            return AstDecl{};
        });
};

struct FileDecl {
    static constexpr auto rule = dsl::p<MessageDecl> | dsl::p<ImportDecl> |
                                 dsl::p<PackageDecl> |
                                 dsl::p<DefaultDecl> >> LEXY_LIT(";");
    static constexpr auto value = lexy::forward<AstDecl>;
};

struct FileDeclList {
    static constexpr auto rule = dsl::list(dsl::p<FileDecl>);
    static constexpr auto value = lexy::as_list<std::vector<AstDecl>>;
};

struct File {
    static constexpr auto whitespace = WS::rule;
    static constexpr auto rule =
        dsl::position + dsl::p<FileDeclList> + dsl::eof;
    static constexpr auto value =
        lexy::callback_with_state<std::shared_ptr<AstFile>>(
            [](ParsingContext const& ctx,
               auto pos,
               std::vector<AstDecl> decls) {
                auto sourceLoc = lexy::get_input_location(*ctx.input, pos);
                return std::make_shared<AstFile>(AstFile{
                    .decls = std::move(decls),
                    .absolutePath = std::string{ctx.filepath},
                    .loc = ctx.getSourceLocation(pos),
                });
            });
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

std::shared_ptr<AstFile> parseToAst(std::string_view path,
                                    std::string_view str,
                                    std::string* errsOut) {
    auto input = lexy::string_input<lexy::utf8_encoding>{str};
    std::stringstream ss;
    auto reporter = lexy_ext::_report_error<StringStreamOutputIterator>{
        ._iter = StringStreamOutputIterator{&ss},
    };
    auto res = lexy::parse<grammar::File>(
        input, grammar::ParsingContext{path, &input}, reporter);
    if (errsOut) {
        *errsOut = ss.str();
    }
    if (!res.has_value())
        return nullptr;

    return res.value();
}
}  // namespace ao::schema
