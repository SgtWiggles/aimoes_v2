#include "AstQueries.h"
#include "Helpers.h"

#include "ao/schema/CompilerContext.h"
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

    CompilerContext ctx{frontend};
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

    CompilerContext ctx{frontend};
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

TEST_CASE(
    "computeDirectives: oneof and nested field directives obey "
    "last-write-wins") {
    SimpleTestFrontend frontend;

    // global default
    AstDirective dg = makeDirective("prof", {{"tag", "global"}});
    AstDirectiveBlock bg = makeDirectiveBlock({dg});
    AstDecl defaultDecl = makeDefaultDeclWithDirectiveBlock(bg);

    // oneof-level directive overrides global
    AstDirective don = makeDirective("prof", {{"tag", "oneofVal"}});
    AstDirectiveBlock bon = makeDirectiveBlock({don});

    // inner field-level directive overrides oneof
    AstDirective din = makeDirective("prof", {{"tag", "innerVal"}});
    AstDirectiveBlock bin = makeDirectiveBlock({din});

    AstField innerField = makeField("inner", 1, makeCtorType(AstBaseType::INT));
    innerField.directives = bin;
    AstFieldDecl innerDecl = makeFieldDecl(innerField);

    AstFieldOneOf oneof = makeOneOf("choice", 10, {innerDecl});
    oneof.directives = bon;

    AstFieldDecl outerDecl;
    outerDecl.field = oneof;

    AstMessage msg = makeMessage("M", {outerDecl});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    auto file = makeFileWithPackageAndDecls("A", "pkg", {defaultDecl, msgDecl});
    frontend.resolvedModules["A"] = file;

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    auto modules = ctx.getModules();
    CHECK(ctx.computeDirectives() == true);

    // locate inner field
    auto& modAst = modules.at("A").ast;
    AstMessage* message = nullptr;
    for (auto& decl : modAst->decls) {
        if (auto m = std::get_if<AstMessage>(&decl.decl)) {
            message = m;
            break;
        }
    }
    REQUIRE(message != nullptr);
    REQUIRE(message->block.fields.size() == 1);

    // Outer decl is oneof, get inner field decl
    auto& outer = message->block.fields[0];
    auto* oneofPtr = std::get_if<AstFieldOneOf>(&outer.field);
    REQUIRE(oneofPtr != nullptr);
    REQUIRE(oneofPtr->block.fields.size() == 1);

    auto& innerFieldDecl = oneofPtr->block.fields[0];
    auto* innerFieldPtr = std::get_if<AstField>(&innerFieldDecl.field);
    REQUIRE(innerFieldPtr != nullptr);

    // Inner field should have effective directive "innerVal"
    REQUIRE(innerFieldPtr->directives.effectiveDirectives.contains("prof"));
    CHECK(innerFieldPtr->directives.effectiveDirectives.at("prof")
              .at("tag")
              .contents == "innerVal");

    // The oneof's own directives.effectiveDirectives should reflect last-write
    REQUIRE(oneofPtr->directives.effectiveDirectives.contains("prof"));
    CHECK(oneofPtr->directives.effectiveDirectives.at("prof")
              .at("tag")
              .contents == "oneofVal");
}

TEST_CASE(
    "computeDirectives: type-level directives inherit and override as "
    "expected") {
    SimpleTestFrontend frontend;

    // global default
    AstDirective dg = makeDirective("prof", {{"tag", "global"}});
    AstDirectiveBlock bg = makeDirectiveBlock({dg});
    AstDecl defaultDecl = makeDefaultDeclWithDirectiveBlock(bg);

    // field-level directive
    AstDirective df = makeDirective("prof", {{"tag", "fieldVal"}});
    AstDirectiveBlock bf = makeDirectiveBlock({df});

    // type-level directive
    AstDirective dt = makeDirective("prof", {{"tag", "typeVal"}});
    AstDirectiveBlock bt = makeDirectiveBlock({dt});

    // field with a user type that has its own directives
    AstTypeName t = makeUserType("Pkg.Target");
    t.directives = bt;

    AstField f = makeField("t", 1, t);
    f.directives = bf;
    AstFieldDecl fd = makeFieldDecl(f);

    AstMessage msg = makeMessage("HasType", {fd});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    auto file = makeFileWithPackageAndDecls("A", "pkg", {defaultDecl, msgDecl});
    frontend.resolvedModules["A"] = file;

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    auto modules = ctx.getModules();
    CHECK(ctx.computeDirectives() == true);

    // find message and field
    AstMessage* message = findFirstMessage(modules.at("A").ast);
    REQUIRE(message != nullptr);
    auto* fieldPtr = std::get_if<AstField>(&message->block.fields[0].field);
    REQUIRE(fieldPtr != nullptr);

    // field effective should be fieldVal (overrides global)
    REQUIRE(fieldPtr->directives.effectiveDirectives.contains("prof"));
    CHECK(fieldPtr->directives.effectiveDirectives.at("prof")
              .at("tag")
              .contents == "fieldVal");

    // type effective should reflect merging/global + field + type with type
    // last-wins
    REQUIRE(fieldPtr->typeName.directives.effectiveDirectives.contains("prof"));
    CHECK(fieldPtr->typeName.directives.effectiveDirectives.at("prof")
              .at("tag")
              .contents == "typeVal");
}