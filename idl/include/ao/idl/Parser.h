#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Ast.h"

namespace ao::idl {
using FileId = uint64_t;
struct SourceFile {
    FileId id;
    std::string displayName;
    uint64_t version;
    std::string contents;
};

enum class ImportResolutionKind { Resolved, Pending, NotFound, Error };
struct ImportResolution {
    ImportResolutionKind kind;
    std::optional<FileId> target;
    std::string message;
};

enum class ImportEdgeState { Pending, Resolved, Failed };

struct ImportEdge {
    ImportPath spec;
    ImportEdgeState state;
    std::optional<FileId> target;
};

struct Diagnostic {
    // DiagnosticSeverity severity;
    // SourceLocation location;
    std::string message;
};

struct Module {
    FileId id;
    std::vector<ImportEdge> imports;

    // std::optional<Ast> ast;
    std::vector<Diagnostic> diagnostics;
};

class ModuleGraph {
   public:
    Module& getOrCreate(FileId);
    void remove(FileId);

    const std::vector<FileId>& dependentsOf(FileId) const;
};

class HostContext {
   public:
    virtual ~HostContext() = default;
    virtual ImportResolution resolveImport(FileId from,
                                           ImportPath const& spec) = 0;
    virtual std::optional<SourceFile> loadImport(FileId) = 0;
};
}  // namespace ao::idl
