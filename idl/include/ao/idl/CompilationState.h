#pragma once

#include "Parser.h"

#include <string>
#include <unordered_map>

namespace ao::idl {
class CompilationState {
   public:
    // For now lets only upsertSource
    void upsertSource(SourceFile const& src);
    void pendingImports() const;

   private:
    std::unordered_map<FileId, Module> m_module;
};
}  // namespace ao::idl
