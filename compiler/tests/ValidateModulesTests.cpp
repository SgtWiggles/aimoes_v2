#include <ao/schema/Ast.h>
#include <ao/schema/CompilerContext.h>

#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace ao::schema;

// Helpers --------------------------------------------------------------------

static AstQualifiedName qnameFromString(std::string const& s) {
    AstQualifiedName q;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '.')) {
        if (!part.empty())
            q.name.push_back(part);
    }
    return q;
}

static SourceLocation locFor(std::string const& path) {
    return SourceLocation{path, 1, 1};
}

static std::shared_ptr<AstFile> makeFileWithPackageAndDecls(
    std::string absolutePath,
    std::optional<std::string> packageName,
    std::vector<AstDecl> decls,
    std::vector<std::string> imports = {}) {
    auto f = std::make_shared<AstFile>();
    f->absolutePath = absolutePath;
    f->loc = locFor(absolutePath);

    // Insert package decl if present
    if (packageName.has_value()) {
        AstDecl pd;
        AstPackageDecl pdDecl;
        pdDecl.name = qnameFromString(*packageName);
        pdDecl.loc = locFor(absolutePath);
        pd.decl = pdDecl;
        pd.loc = pdDecl.loc;
        f->decls.push_back(std::move(pd));
    }

    // Insert imports first (so tests can control ordering if needed)
    for (auto const& imp : imports) {
        AstDecl id;
        AstImport ai;
        ai.path = imp;
        ai.loc = locFor(absolutePath);
        id.decl = ai;
        id.loc = ai.loc;
        f->decls.push_back(std::move(id));
    }

    // Then other declarations
    for (auto& d : decls)
        f->decls.push_back(std::move(d));

    return f;
}

static AstMessage makeMessage(std::string const& name,
                              std::vector<AstFieldDecl> fields = {}) {
    AstMessage m;
    m.name = name;
    m.fields = std::move(fields);
    m.loc = {};  // tests don't rely on exact loc
    return m;
}

static AstFieldDecl makeFieldDecl(AstField field) {
    AstFieldDecl d;
    d.field = std::move(field);
    d.loc = field.loc;
    return d;
}

static AstField makeField(std::string const& name,
                          uint64_t number,
                          AstTypeName type) {
    AstField f;
    f.name = name;
    f.fieldNumber = number;
    f.typeName = std::move(type);
    f.loc = {};  // not used by these tests
    return f;
}

static AstTypeName makeUserType(
    std::string const& qualifiedName,
    std::vector<std::shared_ptr<AstTypeName>> subtypes = {}) {
    AstTypeName t;
    t.type = AstBaseType::USER;
    t.name = qnameFromString(qualifiedName);
    t.subtypes = std::move(subtypes);
    t.loc = {};
    return t;
}

static AstTypeName makeCtorType(
    AstBaseType base,
    std::vector<std::shared_ptr<AstTypeName>> subtypes = {}) {
    AstTypeName t;
    t.type = base;
    t.subtypes = std::move(subtypes);
    t.loc = {};
    return t;
}

// Minimal test frontend that resolves by identity and returns provided
// AstFiles.
class SimpleTestFrontend : public CompilerFrontend {
   public:
    std::unordered_map<std::string, std::shared_ptr<AstFile>> resolvedModules;

    virtual std::expected<std::string, std::string> resolvePath(
        std::string /*currentFile*/,
        std::string path) override {
        // If present in the map, treat resolved name == path
        if (resolvedModules.find(path) != resolvedModules.end())
            return path;
        return std::unexpected<std::string>("could not resolve: " + path);
    }

    virtual std::expected<std::shared_ptr<AstFile>, std::string> loadFile(
        std::string resolvedPath) override {
        auto it = resolvedModules.find(resolvedPath);
        if (it == resolvedModules.end())
            return std::unexpected<std::string>("file not found: " +
                                                resolvedPath);
        return it->second;
    }
};

// Tests ----------------------------------------------------------------------

TEST_CASE("validateModules: succeeds for a single well-formed module") {
    SimpleTestFrontend frontend;

    // A package "pkg" with a single message "M"
    AstDecl mdecl;
    mdecl.decl = makeMessage("M");
    auto fileA = makeFileWithPackageAndDecls("A", "pkg", {mdecl});
    frontend.resolvedModules["A"] = fileA;

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.validateModules() == true);
    CHECK(ctx.getErrorContext().errors.empty());
}

TEST_CASE("validateModules: missing package declaration is reported") {
    SimpleTestFrontend frontend;

    AstDecl mdecl;
    mdecl.decl = makeMessage("M");
    // No package provided
    auto fileA = makeFileWithPackageAndDecls("A", std::nullopt, {mdecl});
    frontend.resolvedModules["A"] = fileA;

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.validateModules() == false);

    bool foundMissingPackage = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MISSING_PACKAGE_DECLARATION)
            foundMissingPackage = true;
    }
    CHECK(foundMissingPackage == true);
}

TEST_CASE("validateModules: multiple package declarations reported") {
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

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.validateModules() == false);

    bool foundMultiple = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MULTIPLE_PACKAGE_DECLARATION)
            foundMultiple = true;
    }
    CHECK(foundMultiple == true);
}

TEST_CASE("validateModules: multiply defined symbol across modules detected") {
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

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("root") == true);

    auto validateResult = ctx.validateModules();
    CHECK(validateResult == false);

    bool foundMultiply = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::MULTIPLY_DEFINED_SYMBOL)
            foundMultiply = true;
    }
    CHECK(foundMultiply == true);
}

TEST_CASE(
    "validateModules: resolves types from dependencies and unqualified names") {
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

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("B") == true);

    // Validation should succeed: B can resolve unqualified name "Target" from A
    CHECK(ctx.validateModules() == true);
    CHECK(ctx.getErrorContext().errors.empty());
}

TEST_CASE(
    "validateModules: undefined and ambiguous type names produce errors") {
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

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("C") == true);
    REQUIRE(ctx.loadFile("D") == true);

    CHECK(ctx.validateModules() == false);

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

TEST_CASE("validateModules: invalid type arguments are reported") {
    SimpleTestFrontend frontend;

    // Message with ARRAY but no subtype -> INVALID_TYPE_ARGS
    AstTypeName arrayNoArg =
        makeCtorType(AstBaseType::ARRAY, {});  // wrong arity
    AstField f = makeField("arr", 1, arrayNoArg);
    AstFieldDecl fd = makeFieldDecl(f);
    AstMessage m = makeMessage("HasBadArray", {fd});
    AstDecl mdecl;
    mdecl.decl = m;

    frontend.resolvedModules["A"] =
        makeFileWithPackageAndDecls("A", "pkg", {mdecl});

    CompilerContext ctx{frontend};
    REQUIRE(ctx.loadFile("A") == true);

    CHECK(ctx.validateModules() == false);

    bool foundInvalidArgs = false;
    for (auto const& e : ctx.getErrorContext().errors) {
        if (e.code == ErrorCode::INVALID_TYPE_ARGS)
            foundInvalidArgs = true;
    }
    CHECK(foundInvalidArgs == true);
}
