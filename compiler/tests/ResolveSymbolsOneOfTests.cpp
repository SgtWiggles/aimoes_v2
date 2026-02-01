#include <ao/schema/Ast.h>
#include <ao/schema/CompilerContext.h>

#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Helpers.h"

using namespace ao::schema;

TEST_CASE(
    "oneof: resolves user type names inside oneof fields from dependency") {
    SimpleTestFrontend frontend;

    // Module A defines pkg.Target
    AstDecl targetDecl;
    targetDecl.decl = makeMessage("Target");
    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {targetDecl});

    // Module B imports A and has a message with a oneof containing a field
    // typed as unqualified "Target"
    AstField inner = makeField("t", 1, makeUserType("Target"));
    AstFieldDecl innerDecl;
    innerDecl.field = inner;
    innerDecl.loc = {};
    AstFieldOneOf oneof;
    oneof.name = "choice";
    oneof.fieldNumber = 100;
    oneof.block.fields.push_back(innerDecl);

    AstFieldDecl outerDecl;
    outerDecl.field = oneof;

    AstMessage msg = makeMessage("HasOneOf", {outerDecl});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "other", {msgDecl}, {"A"});

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("B") == true);

    CHECK(ctx.resolveSymbols() == true);
    CHECK(ctx.getErrorContext().errors.empty());
}

TEST_CASE("oneof: undefined type inside oneof yields SYMBOL_NOT_DEFINED") {
    SimpleTestFrontend frontend;

    // B has a oneof that references DoesNotExist
    AstField inner = makeField("bad", 1, makeUserType("DoesNotExist"));
    AstFieldDecl innerDecl;
    innerDecl.field = inner;
    innerDecl.loc = {};
    AstFieldOneOf oneof;
    oneof.name = "choice";
    oneof.fieldNumber = 10;
    oneof.block.fields.push_back(innerDecl);

    AstFieldDecl outerDecl;
    outerDecl.field = oneof;
    AstMessage msg = makeMessage("UsesMissing", {outerDecl});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "pkg", {msgDecl}, {});

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("B") == true);

    CHECK(ctx.resolveSymbols() == false);

    bool foundNotDefined = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::SYMBOL_NOT_DEFINED)
            foundNotDefined = true;
    }
    CHECK(foundNotDefined == true);
}

TEST_CASE("oneof: ambiguous type inside oneof yields SYMBOL_AMBIGUOUS") {
    SimpleTestFrontend frontend;

    // A and C both define Target
    AstDecl tA;
    tA.decl = makeMessage("Target");
    AstDecl tC;
    tC.decl = makeMessage("Target");
    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg1", {tA});
    frontend.resolvedModules["C"] =
        makeFileWithPackageAndDecls("C", "pkg2", {tC});

    // B imports both A and C and uses unqualified Target inside a oneof
    AstField inner = makeField("amb", 1, makeUserType("Target"));
    AstFieldDecl innerDecl;
    innerDecl.field = inner;
    innerDecl.loc = {};
    AstFieldOneOf oneof;
    oneof.name = "choice";
    oneof.fieldNumber = 5;
    oneof.block.fields.push_back(innerDecl);

    AstFieldDecl outerDecl;
    outerDecl.field = oneof;
    AstMessage msg = makeMessage("UsesAmb", {outerDecl});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "consumer", {msgDecl}, {"A", "C"});

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("B") == true);

    CHECK(ctx.resolveSymbols() == false);

    bool foundAmb = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::SYMBOL_AMBIGUOUS)
            foundAmb = true;
    }
    CHECK(foundAmb == true);
}

TEST_CASE(
    "oneof: invalid type argument inside oneof produces INVALID_TYPE_ARGS") {
    SimpleTestFrontend frontend;

    // oneof contains a field with ARRAY but no subtype (invalid arity)
    AstTypeName arrayNoArg = makeCtorType(AstBaseType::ARRAY, {});
    AstField inner = makeField("arr", 1, arrayNoArg);
    AstFieldDecl innerDecl;
    innerDecl.field = inner;
    innerDecl.loc = {};

    AstFieldOneOf oneof;
    oneof.name = "choice";
    oneof.fieldNumber = 2;
    oneof.block.fields.push_back(innerDecl);

    AstFieldDecl outerDecl;
    outerDecl.field = oneof;
    AstMessage msg = makeMessage("HasBadArray", {outerDecl});
    AstDecl msgDecl;
    msgDecl.decl = msg;

    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {msgDecl});

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.resolveSymbols() == false);

    bool foundInvalidArgs = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::INVALID_TYPE_ARGS)
            foundInvalidArgs = true;
    }
    CHECK(foundInvalidArgs == true);
}