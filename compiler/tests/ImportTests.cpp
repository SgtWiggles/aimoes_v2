#include <ao/schema/Ast.h>
#include <ao/schema/SemanticContext.h>

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

    // Counts how many times loadFile() was invoked for each resolved path.
    std::unordered_map<std::string, int> loadCounts;

    // When a path is present here, resolvePath will succeed even if the file
    // isn't provided in resolvedModules (useful to exercise loadFile failures).
    std::unordered_set<std::string> allowResolveEvenIfMissing;

    virtual std::expected<std::shared_ptr<AstFile>, std::string> loadFile(
        std::string resolvedPath) override {
        loadCounts[resolvedPath]++;

        auto it = resolvedModules.find(resolvedPath);
        if (it == resolvedModules.end())
            return std::unexpected<std::string>("file not found: " +
                                                resolvedPath);
        return it->second;
    }

    std::unordered_map<std::string, std::string> resolveOverrides;

    virtual std::expected<std::string, std::string> resolvePath(
        std::string /*currentFile*/,
        std::string path) override {
        if (auto it = resolveOverrides.find(path); it != resolveOverrides.end())
            return it->second;

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
        SemanticContext ctx{frontend};

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
        SemanticContext ctx{frontend};

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
        SemanticContext ctx{frontend};

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
        SemanticContext ctx{frontend};

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
        SemanticContext ctx{frontend};

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

        SemanticContext ctx{frontend};
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

        SemanticContext ctx{frontend};

        bool ok = ctx.loadFile("A");
        // SemanticContext continues when a dependency cannot be loaded and
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

TEST_CASE("Multiple loadFile calls do not reload already-loaded modules") {
    ImportTestFrontend frontend;

    SECTION("Calling loadFile on the same root twice only loads it once") {
        // A -> B
        frontend.resolvedModules["A"] = makeImportFile("A", {"B"});
        frontend.resolvedModules["B"] = makeImportFile("B", {});

        SemanticContext ctx{frontend};

        CHECK(ctx.loadFile("A") == true);
        CHECK(ctx.loadFile("A") == true);

        // Ensure only one physical load per module
        CHECK(frontend.loadCounts["A"] == 1);
        CHECK(frontend.loadCounts["B"] == 1);

        // Modules present
        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("B") != mods.end());

        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION(
        "Diamond dependency is only loaded once (A -> B,C and B->D, C->D)") {
        frontend.resolvedModules["A"] = makeImportFile("A", {"B", "C"});
        frontend.resolvedModules["B"] = makeImportFile("B", {"D"});
        frontend.resolvedModules["C"] = makeImportFile("C", {"D"});
        frontend.resolvedModules["D"] = makeImportFile("D", {});

        SemanticContext ctx{frontend};

        CHECK(ctx.loadFile("A") == true);

        CHECK(frontend.loadCounts["A"] == 1);
        CHECK(frontend.loadCounts["B"] == 1);
        CHECK(frontend.loadCounts["C"] == 1);
        CHECK(frontend.loadCounts["D"] == 1);  // critical: not 2

        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("B") != mods.end());
        CHECK(mods.find("C") != mods.end());
        CHECK(mods.find("D") != mods.end());

        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION(
        "Shared dependency across different roots is not reloaded (A->C, "
        "B->C)") {
        frontend.resolvedModules["A"] = makeImportFile("A", {"C"});
        frontend.resolvedModules["B"] = makeImportFile("B", {"C"});
        frontend.resolvedModules["C"] = makeImportFile("C", {});

        SemanticContext ctx{frontend};

        CHECK(ctx.loadFile("A") == true);
        CHECK(ctx.loadFile("B") == true);

        // A and B are different roots, but C should be physically loaded once
        CHECK(frontend.loadCounts["A"] == 1);
        CHECK(frontend.loadCounts["B"] == 1);
        CHECK(frontend.loadCounts["C"] == 1);

        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("B") != mods.end());
        CHECK(mods.find("C") != mods.end());

        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION(
        "Transitive pre-load: loading P then loading Q does not reload Q or "
        "its deps") {
        // P -> Q -> R
        frontend.resolvedModules["P"] = makeImportFile("P", {"Q"});
        frontend.resolvedModules["Q"] = makeImportFile("Q", {"R"});
        frontend.resolvedModules["R"] = makeImportFile("R", {});

        SemanticContext ctx{frontend};

        CHECK(ctx.loadFile("P") == true);

        // Q and R are already loaded transitively now:
        CHECK(ctx.loadFile("Q") == true);

        CHECK(frontend.loadCounts["P"] == 1);
        CHECK(frontend.loadCounts["Q"] == 1);  // critical: still 1, not 2
        CHECK(frontend.loadCounts["R"] == 1);  // critical: still 1, not 2

        auto const& mods = ctx.getModules();
        CHECK(mods.find("P") != mods.end());
        CHECK(mods.find("Q") != mods.end());
        CHECK(mods.find("R") != mods.end());

        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION(
        "Direct pre-load: loading B first then loading A (A->B) does not "
        "reload B") {
        frontend.resolvedModules["A"] = makeImportFile("A", {"B"});
        frontend.resolvedModules["B"] = makeImportFile("B", {});

        SemanticContext ctx{frontend};

        CHECK(ctx.loadFile("B") == true);
        CHECK(ctx.loadFile("A") == true);

        CHECK(frontend.loadCounts["B"] == 1);  // critical: not reloaded
        CHECK(frontend.loadCounts["A"] == 1);

        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("B") != mods.end());

        CHECK(ctx.getErrorContext().errors.empty());
    }
}

TEST_CASE("Loading dependency first: B depends on A, load A then B") {
    ImportTestFrontend frontend;

    // B -> A
    frontend.resolvedModules["A"] = makeImportFile("A", {});
    frontend.resolvedModules["B"] = makeImportFile("B", {"A"});

    SemanticContext ctx{frontend};

    SECTION("Load A first, then load B") {
        // Load dependency explicitly
        CHECK(ctx.loadFile("A") == true);

        // Now load the dependent
        CHECK(ctx.loadFile("B") == true);

        // A must NOT be loaded twice
        CHECK(frontend.loadCounts["A"] == 1);
        CHECK(frontend.loadCounts["B"] == 1);

        // Both modules should be present
        auto const& mods = ctx.getModules();
        CHECK(mods.find("A") != mods.end());
        CHECK(mods.find("B") != mods.end());

        // No errors
        CHECK(ctx.getErrorContext().errors.empty());
    }
}

TEST_CASE(
    "Module.dependencies contains only resolved unique dependency names") {
    ImportTestFrontend frontend;

    SECTION("Unresolved imports are NOT added to dependencies") {
        // A imports MISSING, but resolvePath will fail for MISSING
        frontend.resolvedModules["A"] = makeImportFile("A", {"MISSING"});

        SemanticContext ctx{frontend};
        REQUIRE(ctx.loadFile("A") == true);

        auto const& mods = ctx.getModules();
        REQUIRE(mods.find("A") != mods.end());

        // Since MISSING could not be resolved, it must not appear.
        CHECK(mods.at("A").dependencies.empty());

        bool foundFailResolve = false;
        for (auto const& e : ctx.getErrorContext().errors) {
            if (e.code == ErrorCode::FAILED_TO_RESOLVE_IMPORT)
                foundFailResolve = true;
        }
        CHECK(foundFailResolve == true);
    }

    SECTION(
        "Dependencies store the RESOLVED unique name, not the raw import "
        "string") {
        // A imports "./B" but it resolves to unique name "B"
        frontend.resolvedModules["A"] = makeImportFile("A", {"./B"});
        frontend.resolvedModules["B"] = makeImportFile("B", {});

        frontend.resolveOverrides["./B"] = "B";

        SemanticContext ctx{frontend};
        REQUIRE(ctx.loadFile("A") == true);

        auto const& mods = ctx.getModules();
        REQUIRE(mods.find("A") != mods.end());
        REQUIRE(mods.find("B") != mods.end());

        // Must contain resolved name "B", not "./B"
        CHECK(mods.at("A").dependencies.size() == 1);
        CHECK(mods.at("A").dependencies.contains("B"));
        CHECK(mods.at("A").dependencies.contains("./B") == false);

        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION(
        "Multiple imports that resolve to the SAME unique module only appear "
        "once") {
        // A imports "pkg:B" and "./B", both resolve to unique "B"
        frontend.resolvedModules["A"] = makeImportFile("A", {"pkg:B", "./B"});
        frontend.resolvedModules["B"] = makeImportFile("B", {});

        frontend.resolveOverrides["pkg:B"] = "B";
        frontend.resolveOverrides["./B"] = "B";

        SemanticContext ctx{frontend};
        REQUIRE(ctx.loadFile("A") == true);

        auto const& mods = ctx.getModules();
        REQUIRE(mods.find("A") != mods.end());
        REQUIRE(mods.find("B") != mods.end());

        // Set should dedupe to a single resolved dependency "B"
        CHECK(mods.at("A").dependencies.size() == 1);
        CHECK(mods.at("A").dependencies.contains("B"));

        CHECK(ctx.getErrorContext().errors.empty());
    }

    SECTION(
        "Multiple distinct resolved dependencies are recorded, each by unique "
        "resolved name") {
        // A imports "./B" -> "B" and "./C" -> "C"
        frontend.resolvedModules["A"] = makeImportFile("A", {"./B", "./C"});
        frontend.resolvedModules["B"] = makeImportFile("B", {});
        frontend.resolvedModules["C"] = makeImportFile("C", {});

        frontend.resolveOverrides["./B"] = "B";
        frontend.resolveOverrides["./C"] = "C";

        SemanticContext ctx{frontend};
        REQUIRE(ctx.loadFile("A") == true);

        auto const& mods = ctx.getModules();
        REQUIRE(mods.find("A") != mods.end());
        REQUIRE(mods.find("B") != mods.end());
        REQUIRE(mods.find("C") != mods.end());

        CHECK(mods.at("A").dependencies.size() == 2);
        CHECK(mods.at("A").dependencies.contains("B"));
        CHECK(mods.at("A").dependencies.contains("C"));

        // Raw import strings should not appear
        CHECK(mods.at("A").dependencies.contains("./B") == false);
        CHECK(mods.at("A").dependencies.contains("./C") == false);

        CHECK(ctx.getErrorContext().errors.empty());
    }
}
