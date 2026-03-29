#pragma once

#include "Error.h"
#include "IR.h"

#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>

namespace ao::schema::cpp {
struct OutputFiles {
    // Generates to includeRoot/outDir/projectName
    // Includes will generate without include root
    std::string projectName;
    std::filesystem::path outDir;
    std::filesystem::path root;

    std::function<std::unique_ptr<std::ostream>(std::filesystem::path openFile,
                                                std::ios_base::openmode mode,
                                                ErrorContext& errs)>
        loader;
};
bool generateCppCode(ir::IR const& ir, ErrorContext& errs, OutputFiles& files);

}  // namespace ao::schema::cpp
