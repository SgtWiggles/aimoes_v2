#include "Helpers.h"

#include "ao/schema/SemanticContext.h"
#include "ao/schema/Error.h"

#include <catch2/catch_all.hpp>

using namespace ao::schema;

TEST_CASE("validateIds: succeeds for unique message ids and field numbers") {
    SimpleTestFrontend frontend;

    // Message with unique fields
    AstField f1 = makeField("a", 1, makeCtorType(AstBaseType::INT));
    AstField f2 = makeField("b", 2, makeCtorType(AstBaseType::UINT));
    AstFieldDecl fd1 = makeFieldDecl(f1);
    AstFieldDecl fd2 = makeFieldDecl(f2);
    AstMessage m = makeMessage("Msg", {fd1, fd2}, std::optional<uint64_t>{1});
    AstDecl decl;
    decl.decl = m;

    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {decl});

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.validateIds() == true);
    CHECK(ctx.getErrorContext().errors.empty());
}

TEST_CASE("validateIds: multiply defined message id across modules detected") {
    SimpleTestFrontend frontend;

    AstMessage ma = makeMessage("M1", {}, std::optional<uint64_t>{42});
    AstDecl da;
    da.decl = ma;
    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {da});

    AstMessage mb = makeMessage("M2", {}, std::optional<uint64_t>{42});
    AstDecl db;
    db.decl = mb;
    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "pkg2", {db});

    // root imports both
    frontend.resolvedModules["root"] =
        makeFileWithPackageAndDecls("root", "r", {}, {"A", "B"});

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("root") == true);

    auto validateResult = ctx.validateIds();
    CHECK(validateResult == false);

    bool found = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MULTIPLY_DEFINED_MESSAGE_ID)
            found = true;
    }
    CHECK(found == true);
}

TEST_CASE("validateIds: multiply defined field id within a message detected") {
    SimpleTestFrontend frontend;

    AstField f1 = makeField("x", 1, makeCtorType(AstBaseType::INT));
    AstField f2 =
        makeField("y", 1, makeCtorType(AstBaseType::UINT));  // duplicate id
    AstFieldDecl fd1 = makeFieldDecl(f1);
    AstFieldDecl fd2 = makeFieldDecl(f2);
    AstMessage m = makeMessage("DuplicateField", {fd1, fd2});
    AstDecl decl;
    decl.decl = m;

    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {decl});

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.validateIds() == false);

    bool found = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MULTIPLY_DEFINED_FIELD_ID)
            found = true;
    }
    CHECK(found == true);
}

TEST_CASE(
    "validateIds: reserved overlapping allowed; field colliding with reserved "
    "errors") {
    SimpleTestFrontend frontend;

    // Two reserved entries that overlap -> allowed
    AstFieldDecl res1 = makeFieldDeclReserved({10, 11});
    AstFieldDecl res2 = makeFieldDeclReserved({11, 12});
    AstMessage mOk = makeMessage("WithReservedOnly", {res1, res2});
    AstDecl dOk;
    dOk.decl = mOk;
    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {dOk});

    SemanticContext ctxOk{frontend};
    REQUIRE(ctxOk.loadFile("A") == true);
    CHECK(ctxOk.validateIds() == true);

    // Now reserved + a field using reserved id -> error
    AstFieldDecl res = makeFieldDeclReserved({5});
    AstField f = makeField("conflict", 5, makeCtorType(AstBaseType::INT));
    AstFieldDecl fd = makeFieldDecl(f);
    AstMessage mErr = makeMessage("ReservedConflict", {res, fd});
    AstDecl dErr;
    dErr.decl = mErr;
    frontend.resolvedModules["B"] =
        makeFileWithPackageAndDecls("B", "pkg2", {dErr});

    SemanticContext ctxErr{frontend};
    REQUIRE(ctxErr.loadFile("B") == true);
    CHECK(ctxErr.validateIds() == false);

    bool found = false;
    for (auto const& e : ctxErr.getErrorContext().errors) {
        if (e.code == ErrorCode::MULTIPLY_DEFINED_FIELD_ID)
            found = true;
    }
    CHECK(found == true);
}