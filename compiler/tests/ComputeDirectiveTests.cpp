#include "AstQueries.h"
#include "Helpers.h"

#include "ao/schema/Error.h"
#include "ao/schema/SemanticContext.h"

#include <catch2/catch_all.hpp>

using namespace ao::schema;

// Helper to find the first AstMessage in a module AST
static AstMessage* findFirstMessage(std::shared_ptr<AstFile> f) {
    for (auto& decl : f->decls) {
        if (auto msg = std::get_if<AstMessage>(&decl.decl))
            return msg;
    }
    return nullptr;
}

std::vector<std::string> getDirectiveTag(
    std::vector<std::pair<std::string, AstValueLiteral>> const& tags,
    std::string_view targetTag) {
    std::vector<std::string> ret;
    for (auto const& [tag, value] : tags) {
        ret.emplace_back(value.contents);
    }
    return ret;
}

TEST_CASE(
    "computeDirectives: global default flows to field effectiveDirectives") {
    SimpleTestFrontend frontend;

    // global default: profile "prof" tag "tag" -> "global"
    AstDirective d = makeDirective("prof", {{"tag", "global"}});
    AstDirectiveBlock block = makeDirectiveBlock({d});
    AstDecl defaultDecl = makeDefaultDeclWithDirectiveBlock(block);

    // message with a simple field
    AstField f = makeField("f", 1, makeCtorType(AstBaseType::INT));
    AstFieldDecl fd = makeFieldDecl(f);
    AstMessage msg = makeMessage("M", {fd});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    auto file = makeFileWithPackageAndDecls("A", "pkg", {defaultDecl, msgDecl});
    frontend.resolvedModules["A"] = file;

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    // copy modules to mutable map and compute directives
    auto modules = ctx.getModules();
    CHECK(ctx.computeDirectives() == true);

    auto& mod = modules.at("A");
    auto* message = findFirstMessage(mod.ast);
    REQUIRE(message != nullptr);

    auto& fieldDecl = message->block.fields[0];
    auto* field = std::get_if<AstField>(&fieldDecl.field);
    REQUIRE(field != nullptr);

    auto const& eff = field->directives.effectiveDirectives;
    REQUIRE(eff.contains("prof"));
    auto tag = getDirectiveTag(eff.at("prof"), "tag");
    auto expected = std::vector<std::string>{"global"};
    REQUIRE(tag == expected);
}

TEST_CASE(
    "computeDirectives: field-local are appended and show up after the global "
    "tags") {
    SimpleTestFrontend frontend;

    // global default
    AstDirective dg = makeDirective("prof", {{"tag", "global"}});
    AstDirectiveBlock bg = makeDirectiveBlock({dg});
    AstDecl defaultDecl = makeDefaultDeclWithDirectiveBlock(bg);

    // field-local directive with same profile/tag overrides
    AstDirective df = makeDirective("prof", {{"tag", "fieldVal"}});
    AstDirectiveBlock bf = makeDirectiveBlock({df});

    AstField f = makeField("f", 1, makeCtorType(AstBaseType::INT));
    f.directives = bf;  // field-level directives
    AstFieldDecl fd = makeFieldDecl(f);
    AstMessage msg = makeMessage("M", {fd});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    auto file = makeFileWithPackageAndDecls("A", "pkg", {defaultDecl, msgDecl});
    frontend.resolvedModules["A"] = file;

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    auto modules = ctx.getModules();
    CHECK(ctx.computeDirectives() == true);

    auto field = query::findFieldInModule(modules, "A", "M", "f");
    REQUIRE(field);
    REQUIRE(field->directives.effectiveDirectives.contains("prof"));

    auto tags = getDirectiveTag(
        field->directives.effectiveDirectives.at("prof"), "tag");
    auto expected = std::vector<std::string>{"global", "fieldVal"};
    REQUIRE(tags == expected);
}
