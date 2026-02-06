#include "AstQueries.h"
#include "Helpers.h"

#include "ao/schema/SemanticContext.h"
#include "ao/schema/Error.h"

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
    REQUIRE(eff.at("prof").contains("tag"));
    CHECK(eff.at("prof").at("tag").contents == "global");
}

TEST_CASE(
    "computeDirectives: field-local overrides global default (last write "
    "wins)") {
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
    REQUIRE(
        field->directives.effectiveDirectives.at("prof").at("tag").contents ==
        "fieldVal");
}
