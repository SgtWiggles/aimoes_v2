#include <ao/schema/Ast.h>
#include <ao/schema/CompilerContext.h>

#include <catch2/catch_all.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ao::schema;

// A tiny test frontend that returns prebuilt AstFile objects and resolves
// import paths. For tests we allow forcing a resolve even if the file isn't
// present via `allowResolveEvenIfMissing`.
class ImportTestFrontend : public CompilerFrontend {
   public:
    // Map of resolved path -> AstFile
    std::unordered_map<std::string, std::shared_ptr<AstFile>> resolvedModules;

    // When a path is present here, resolvePath will succeed even if the file
    // isn't provided in resolvedModules (useful to exercise loadFile failures).
    std::unordered_set<std::string> allowResolveEvenIfMissing;

    virtual std::expected<std::shared_ptr<AstFile>, std::string> loadFile(
        std::string resolvedPath) override {
        auto it = resolvedModules.find(resolvedPath);
        if (it == resolvedModules.end())
            return std::unexpected<std::string>("file not found: " +
                                                resolvedPath);
        return it->second;
    }

    virtual std::expected<std::string, std::string> resolvePath(
        std::string /*currentFile*/,
        std::string path) override {
        if (resolvedModules.find(path) != resolvedModules.end())
            return path;
        if (allowResolveEvenIfMissing.find(path) !=
            allowResolveEvenIfMissing.end())
            return path;
        return std::unexpected<std::string>("could not resolve: " + path);
    }
};

// Helper: create an AstFile with the given absolutePath and a list of imports
static std::shared_ptr<AstFile> makeImportFile(
    std::string absolutePath,
    std::vector<std::string> imports) {
    auto f = std::make_shared<AstFile>();
    f->absolutePath = absolutePath;
    f->loc = SourceLocation{absolutePath, 1, 1};
    for (auto const& imp : imports) {
        AstDecl decl;
        AstImport ai;
        ai.path = imp;
        ai.loc = SourceLocation{absolutePath, 1, 1};
        decl.decl = ai;
        decl.loc = ai.loc;
        f->decls.push_back(std::move(decl));
    }
    return f;
}

TEST_CASE("Import cycles and passing cases") {
    ImportTestFrontend frontend;

    // Build some test graphs (keys are "A", "B", "C", "D", "E")
    // Passing small graph: A -> B
    frontend.resolvedModules["A"] = makeImportFile("A", {"B"});
    frontend.resolvedModules["B"] = makeImportFile("B", {});

    // Passing larger graph: P -> Q, R ; Q -> S ; R -> (none) ; S -> (none)
    frontend.resolvedModules["P"] = makeImportFile("P", {"Q", "R"});
    frontend.resolvedModules["Q"] = makeImportFile("Q", {"S"});
    frontend.resolvedModules["R"] = makeImportFile("R", {});
    frontend.resolvedModules["S"] = makeImportFile("S", {});

    // Cycle of size 2: C <-> D
    frontend.resolvedModules["C"] = makeImportFile("C", {"D"});
    frontend.resolvedModules["D"] = makeImportFile("D", {"C"});

    // Cycle of size 3: X -> Y -> Z -> X
    frontend.resolvedModules["X"] = makeImportFile("X", {"Y"});
    frontend.resolvedModules["Y"] = makeImportFile("Y", {"Z"});
    frontend.resolvedModules["Z"] = makeImportFile("Z", {"X"});

    SECTION("Simple passing import (A -> B)") {
        CompilerContext ctx{frontend};

        bool ok = ctx.loadFile("A");
        CHECK(ok == true);

        // Both A and B should be present
        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("B") != mods.end());

        // No errors
        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION("Larger acyclic graph (P -> Q,R, Q -> S)") {
        CompilerContext ctx{frontend};

        bool ok = ctx.loadFile("P");
        CHECK(ok == true);

        auto const& mods = ctx.getModules();
        CHECK(mods.find("P") != mods.end());
        CHECK(mods.find("Q") != mods.end());
        CHECK(mods.find("R") != mods.end());
        CHECK(mods.find("S") != mods.end());

        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION("Cycle of size 2 (C <-> D) is detected") {
        CompilerContext ctx{frontend};

        bool ok = ctx.loadFile("C");
        CHECK(ok == false);

        // Expect at least one CYCLICAL_IMPORT error
        bool foundCycle = false;
        for (auto const& e : ctx.getErrorContext().errors) {
            if (e.code == ErrorCode::CYCLICAL_IMPORT)
                foundCycle = true;
        }
        CHECK(foundCycle == true);
    }

    SECTION("Cycle of size 3 (X -> Y -> Z -> X) is detected") {
        CompilerContext ctx{frontend};

        bool ok = ctx.loadFile("X");
        CHECK(ok == false);

        bool foundCycle = false;
        for (auto const& e : ctx.getErrorContext().errors) {
            if (e.code == ErrorCode::CYCLICAL_IMPORT)
                foundCycle = true;
        }
        CHECK(foundCycle == true);
    }
}

TEST_CASE("Unfound import cases") {
    ImportTestFrontend frontend;

    // Case: root not found at resolveRoot time
    SECTION(
        "Root resolution failure returns false and reports "
        "FAILED_TO_RESOLVE_IMPORT") {
        CompilerContext ctx{frontend};

        bool ok = ctx.loadFile("NONEXISTENT_ROOT");
        CHECK(ok == false);

        bool foundFailResolve = false;
        for (auto const& e : ctx.getErrorContext().errors) {
            if (e.code == ErrorCode::FAILED_TO_RESOLVE_IMPORT)
                foundFailResolve = true;
        }
        CHECK(foundFailResolve == true);
    }

    // Case: dependency fails to resolve (resolvePath fails)
    SECTION("Dependency resolution failure is reported but root still loads") {
        // A imports MISSING, but MISSING cannot be resolved
        frontend.resolvedModules.clear();
        frontend.resolvedModules["A"] = makeImportFile("A", {"MISSING"});

        CompilerContext ctx{frontend};
        bool ok = ctx.loadFile("A");
        // loadFile should succeed for A even though one import couldn't be
        // resolved
        CHECK(ok == true);

        // A should be present, MISSING should not
        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("MISSING") == mods.end());

        bool foundFailResolve = false;
        for (auto const& e : ctx.getErrorContext().errors) {
            if (e.code == ErrorCode::FAILED_TO_RESOLVE_IMPORT)
                foundFailResolve = true;
        }
        CHECK(foundFailResolve == true);
    }

    // Case: dependency resolves but loadFile fails (syntax/load error)
    SECTION(
        "Dependency load failure yields SYNTAX_ERROR and root still loads") {
        frontend.resolvedModules.clear();
        // A imports B; allow resolve of B, but do not provide B in
        // resolvedModules
        frontend.resolvedModules["A"] = makeImportFile("A", {"B"});
        frontend.allowResolveEvenIfMissing.insert("B");

        CompilerContext ctx{frontend};

        bool ok = ctx.loadFile("A");
        // CompilerContext continues when a dependency cannot be loaded and
        // ultimately returns true (root loaded) but records the error.
        CHECK(ok == true);

        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("B") == mods.end());

        bool foundSyntax = false;
        for (auto const& e : ctx.getErrorContext().errors) {
            if (e.code == ErrorCode::SYNTAX_ERROR)
                foundSyntax = true;
        }
        CHECK(foundSyntax == true);
    }
}
