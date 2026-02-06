#include <ao/schema/Ast.h>
#include <ao/schema/SemanticContext.h>

#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Helpers.h"

using namespace ao::schema;

TEST_CASE("resolveSymbols: succeeds for a single well-formed module") {
    SimpleTestFrontend frontend;

    // A package "pkg" with a single message "M"
    AstDecl mdecl;
    mdecl.decl = makeMessage("M");
    auto fileA = makeFileWithPackageAndDecls("A", "pkg", {mdecl});
    frontend.resolvedModules["A"] = fileA;

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.resolveSymbols() == true);
    CHECK(ctx.getErrorContext().errors.empty());
}

TEST_CASE("resolveSymbols: missing package declaration is reported") {
    SimpleTestFrontend frontend;

    AstDecl mdecl;
    mdecl.decl = makeMessage("M");
    // No package provided
    auto fileA = makeFileWithPackageAndDecls("A", std::nullopt, {mdecl});
    frontend.resolvedModules["A"] = fileA;

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.resolveSymbols() == false);

    bool foundMissingPackage = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MISSING_PACKAGE_DECLARATION)
            foundMissingPackage = true;
    }
    CHECK(foundMissingPackage == true);
}

TEST_CASE("resolveSymbols: multiple package declarations reported") {
    SimpleTestFrontend frontend;

    // Build a file that contains two package declarations
    AstDecl pd1;
    pd1.decl = AstPackageDecl{qnameFromString("pkg"), locFor("A")};
    pd1.loc = locFor("A");

    AstDecl pd2;
    pd2.decl = AstPackageDecl{qnameFromString("pkg2"), locFor("A")};
    pd2.loc = locFor("A");

    auto fileA = std::make_shared<AstFile>();
    fileA->absolutePath = "A";
    fileA->loc = locFor("A");
    fileA->decls.push_back(pd1);
    fileA->decls.push_back(pd2);

    frontend.resolvedModules["A"] = fileA;

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.resolveSymbols() == false);

    bool foundMultiple = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MULTIPLE_PACKAGE_DECLARATION)
            foundMultiple = true;
    }
    CHECK(foundMultiple == true);
}

TEST_CASE("resolveSymbols: multiply defined symbol across modules detected") {
    SimpleTestFrontend frontend;

    // A and B both export pkg.M
    AstDecl msgA;
    msgA.decl = makeMessage("M");
    AstDecl msgB;
    msgB.decl = makeMessage("M");
    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {msgA});
    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "pkg", {msgB});

    // root imports A and B so both modules are loaded
    frontend.resolvedModules["root"] =
        makeFileWithPackageAndDecls("root", "rootpkg", {}, {"A", "B"});

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("root") == true);

    auto validateResult = ctx.resolveSymbols();
    CHECK(validateResult == false);

    bool foundMultiply = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MULTIPLY_DEFINED_SYMBOL)
            foundMultiply = true;
    }
    CHECK(foundMultiply == true);
}

TEST_CASE(
    "resolveSymbols: resolves types from dependencies and unqualified names") {
    SimpleTestFrontend frontend;

    // Module A defines pkg.Target
    AstDecl targetDecl;
    targetDecl.decl = makeMessage("Target");
    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {targetDecl});

    // Module B imports A and defines a message Use that has a field typed as
    // unqualified "Target"
    AstField f = makeField("t", 1, makeUserType("Target"));
    AstFieldDecl fd = makeFieldDecl(f);
    AstMessage useMsg = makeMessage("Use", {fd});
    AstDecl useDecl;
    useDecl.decl = useMsg;

    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "other", {useDecl}, {"A"});

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("B") == true);

    // Validation should succeed: B can resolve unqualified name "Target" from A
    CHECK(ctx.resolveSymbols() == true);
    CHECK(ctx.getErrorContext().errors.empty());
}

TEST_CASE("resolveSymbols: undefined and ambiguous type names produce errors") {
    SimpleTestFrontend frontend;

    // Module A: pkg1.Target
    AstDecl t1;
    t1.decl = makeMessage("Target");
    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg1", {t1});

    // Module B: pkg2.Target
    AstDecl t2;
    t2.decl = makeMessage("Target");
    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "pkg2", {t2});

    // Module C imports A and B and uses unqualified "Target" -> ambiguous
    AstField ambField = makeField("amb", 1, makeUserType("Target"));
    AstFieldDecl ambFd = makeFieldDecl(ambField);
    AstMessage useAmb = makeMessage("UseAmb", {ambFd});
    AstDecl useAmbDecl;
    useAmbDecl.decl = useAmb;
    frontend.resolvedModules["C"] =
        makeFileWithPackageAndDecls("C", "consumer", {useAmbDecl}, {"A", "B"});

    // Module D references a non-existent type -> SYMBOL_NOT_DEFINED
    AstField badField = makeField("bad", 1, makeUserType("DoesNotExist"));
    AstFieldDecl badFd = makeFieldDecl(badField);
    AstMessage useBad = makeMessage("UseBad", {badFd});
    AstDecl useBadDecl;
    useBadDecl.decl = useBad;
    frontend.resolvedModules["D"] =
        makeFileWithPackageAndDecls("D", "consumer2", {useBadDecl}, {"A"});

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("C") == true);
    REQUIRE(ctx.loadFile("D") == true);

    CHECK(ctx.resolveSymbols() == false);

    bool foundAmb = false;
    bool foundNotDefined = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::SYMBOL_AMBIGUOUS)
            foundAmb = true;
        if (e.code == ErrorCode::SYMBOL_NOT_DEFINED)
            foundNotDefined = true;
    }
    CHECK(foundAmb == true);
    CHECK(foundNotDefined == true);
}

TEST_CASE("resolveSymbols: invalid type arguments are reported") {
    SimpleTestFrontend frontend;

    // Message with ARRAY but no subtype -> INVALID_TYPE_ARGS
    AstType arrayNoArg = makeCtorType(AstBaseType::ARRAY, {});  // wrong arity
    AstField f = makeField("arr", 1, arrayNoArg);
    AstFieldDecl fd = makeFieldDecl(f);
    AstMessage m = makeMessage("HasBadArray", {fd});
    AstDecl mdecl;
    mdecl.decl = m;

    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {mdecl});

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.resolveSymbols() == false);

    bool foundInvalidArgs = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::INVALID_TYPE_ARGS)
            foundInvalidArgs = true;
    }
    CHECK(foundInvalidArgs == true);
}
