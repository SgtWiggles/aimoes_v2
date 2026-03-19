#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include "ao/schema/CppBackend.h"
#include "ao/schema/IR.h"
#include "ao/schema/Parser.h"
#include "ao/schema/SemanticContext.h"

namespace fs = std::filesystem;
namespace po = boost::program_options;

using ao::schema::AstFile;
using ao::schema::AstQualifiedName;
using ao::schema::CompilerFrontend;
using ao::schema::ErrorContext;
using ao::schema::SemanticContext;

// Simple file-based frontend for the compiler frontend interface.
class FileFrontend : public CompilerFrontend {
   public:
    explicit FileFrontend(std::vector<std::string> includePaths)
        : m_includePaths(std::move(includePaths)) {
        if (m_includePaths.empty())
            m_includePaths.push_back(".");
    }

    std::expected<std::shared_ptr<AstFile>, std::string> loadFile(
        std::string resolvedPath) override {
        std::ifstream ifs(resolvedPath, std::ios::in | std::ios::binary);
        if (!ifs)
            return std::unexpected(std::string("Failed to open file: ") +
                                   resolvedPath);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string content = ss.str();
        std::string parseErrors;
        auto ast = ao::schema::parseToAst(resolvedPath, content, &parseErrors);
        if (!ast)
            return std::unexpected(parseErrors.empty()
                                       ? std::string("Failed to parse file")
                                       : parseErrors);
        return ast;
    }

    static bool containsPathSeparator(std::string const& s) {
        return s.find('/') != std::string::npos ||
               s.find('\\') != std::string::npos;
    }
    static bool endsWithAosl(std::string const& s) {
        if (s.size() < 5)
            return false;
        auto tail = s.substr(s.size() - 5);
        // case-sensitive match for ".aosl"
        return tail == ".aosl";
    }

    std::expected<std::string, std::string> resolveModule(
        AstQualifiedName moduleName) override {
        // If the requested module looks like a direct path (contains path sep
        // or ends with .aosl) treat it as a path.
        auto moduleStr = moduleName.toString();
        if (containsPathSeparator(moduleStr) || endsWithAosl(moduleStr)) {
            if (fs::exists(moduleStr))
                return moduleStr;
            return std::unexpected(std::string("Module file not found: ") +
                                   moduleStr);
        }

        // Build relative path from module name parts: hello.world.a ->
        // hello/world/a
        std::string rel;
        for (size_t i = 0; i < moduleName.name.size(); ++i) {
            if (i != 0)
                rel += fs::path::preferred_separator;
            rel += moduleName.name[i];
        }

        // Collect matching candidates across include paths for
        // <include>/rel.aosl or <include>/rel
        std::vector<std::string> candidates;
        for (auto const& inc : m_includePaths) {
            fs::path base = inc;
            fs::path candidate1 = base / rel;
            candidate1 += ".aosl";
            if (fs::exists(candidate1))
                candidates.push_back(candidate1.string());
            fs::path candidate2 = base / rel;
            if (fs::exists(candidate2))
                candidates.push_back(candidate2.string());
        }

        if (candidates.empty())
            return std::unexpected(std::string("Module not found: ") +
                                   moduleStr);
        if (candidates.size() > 1) {
            std::ostringstream ss;
            ss << "Ambiguous module lookup for '" << moduleStr
               << "', candidates:";
            for (auto const& c : candidates)
                ss << "\n " << c;
            return std::unexpected(ss.str());
        }

        return candidates.front();
    }

   private:
    std::vector<std::string> m_includePaths;
};

int main(int argc, char** argv) {
    try {
        po::options_description desc("Allowed options");
        desc.add_options()("help,h", "Show this help message")(
            "include,I", po::value<std::vector<std::string>>()->composing(),
            "Add include path")("out-header", po::value<std::string>(),
                                "Output header file path")(
            "out-ir", po::value<std::string>(), "Output IR file path")(
            "pos", po::value<std::vector<std::string>>(),
            "Positional arguments: <inputs...>");

        po::positional_options_description pod;
        pod.add("pos", -1);

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv)
                      .options(desc)
                      .positional(pod)
                      .run(),
                  vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << "Usage: aosl-compiler [-I include_path]... "
                         "--out-header <file> --out-ir <file> <inputs...>\n\n";
            std::cout << desc << "\n";
            return 0;
        }

        std::vector<std::string> posArgs;
        if (vm.count("pos"))
            posArgs = vm["pos"].as<std::vector<std::string>>();

        if (posArgs.empty()) {
            std::cerr << "Usage: aosl-compiler [-I include_path]... "
                         "--out-header <file> --out-ir <file> <inputs...>\n";
            return 2;
        }

        if (!vm.count("out-header") || !vm.count("out-ir")) {
            std::cerr << "Both --out-header and --out-ir must be provided\n";
            return 2;
        }

        std::string outHeader = vm["out-header"].as<std::string>();
        std::string outIr = vm["out-ir"].as<std::string>();

        // Inputs are all positional arguments
        std::vector<std::string> inputs = posArgs;

        std::vector<std::string> includePaths;
        if (vm.count("include"))
            includePaths = vm["include"].as<std::vector<std::string>>();

        if (includePaths.empty())
            includePaths.push_back(".");

        // Validate inputs exist (directories or files) and expand directories
        std::vector<std::string> expandedInputs;
        for (auto const& arg : inputs) {
            fs::path p = arg;
            if (!fs::exists(p)) {
                std::cerr << "Input not found: " << arg << "\n";
                return 2;
            }
            if (fs::is_directory(p)) {
                for (auto const& ent : fs::recursive_directory_iterator(p)) {
                    if (!ent.is_regular_file())
                        continue;
                    if (ent.path().extension() == ".aosl")
                        expandedInputs.push_back(ent.path().string());
                }
            } else {
                expandedInputs.push_back(fs::absolute(p).string());
            }
        }

        if (expandedInputs.empty()) {
            std::cerr << "No input files found\n";
            return 2;
        }

        FileFrontend frontend(includePaths);
        SemanticContext ctx(frontend);

        // Load all input files as root modules. Use their paths so
        // resolveModule will accept direct paths (handled in resolveModule
        // above).
        for (auto const& in : expandedInputs) {
            if (!ctx.loadFile(in)) {
                std::cerr << "Failed to load module: " << in << "\n";
                std::cerr << ctx.getErrorContext().toString();
                return 3;
            }
        }

        if (!ctx.validate()) {
            std::cerr << "Validation failed:\n"
                      << ctx.getErrorContext().toString();
            return 4;
        }

        ErrorContext errs;
        auto ir = ao::schema::ir::generateIR(ctx.getModules(), errs);
        if (!errs.ok()) {
            std::cerr << "IR generation failed:\n" << errs.toString();
            return 5;
        }

        std::ofstream headerStream(outHeader, std::ios::out);
        if (!headerStream) {
            std::cerr << "Failed to open output header: " << outHeader << "\n";
            return 6;
        }
        std::ofstream irStream(outIr, std::ios::out | std::ios::binary);
        if (!irStream) {
            std::cerr << "Failed to open output IR file: " << outIr << "\n";
            return 7;
        }

        ao::schema::cpp::OutputFiles outFiles(headerStream, irStream);
        bool ok = ao::schema::cpp::generateCppCode(ir, errs, outFiles);
        if (!ok || !errs.ok()) {
            std::cerr << "Code generation failed:\n" << errs.toString();
            return 8;
        }

        std::cout << "Generated " << outHeader << " and " << outIr << "\n";
        return 0;
    } catch (const po::error& e) {
        std::cerr << "Argument error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
