#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>

#include "PathConfig.h"

namespace fs = std::filesystem;

static std::string runCommand(std::string const& cmd) {
 std::string out;
 FILE* pipe = _popen(cmd.c_str(), "r");
 if (!pipe) return "";
 char buffer[256];
 while (fgets(buffer, sizeof(buffer), pipe)) {
 out += buffer;
 }
 _pclose(pipe);
 return out;
}

TEST_CASE("CLI generates C++ header and IR and compiles the header", "[cli][integration]") {
 fs::path tmp = fs::temp_directory_path() / "aosl_cli_test";
 fs::remove_all(tmp);
 fs::create_directories(tmp);

 // Create a minimal .aosl file
 fs::path src = tmp / "test.aosl";
 std::ofstream ofs(src);
 ofs << "package a;\nmessage42 test{123 f int; }\n";
 ofs.close();

 fs::path outHeader = tmp / "gen.h";
 fs::path outIr = tmp / "gen.ir";

 std::string cmd = std::string(AOSL_COMPILER_EXE) + " --out-header " +
 outHeader.string() + " --out-ir " + outIr.string() +
 " " + src.string();

 auto out = runCommand(cmd);
 INFO(out);

 REQUIRE(fs::exists(outHeader));
 REQUIRE(fs::exists(outIr));

 // Try to compile a small translation unit that includes the generated header
 fs::path tu = tmp / "test_compile.cpp";
 std::ofstream tufs(tu);
 tufs << "#include <iostream>\n";
 tufs << "#include \"" << outHeader.string() << "\"\n";
 tufs << "int main() { std::cout << \"ok\"; return0; }\n";
 tufs.close();

 // Use cl or g++ depending on environment
#ifdef _MSC_VER
 std::string compileCmd = std::string("cl /nologo /std:c++23 ") + tu.string() +
 " /Fe" + (tmp / "a.exe").string();
#else
 std::string compileCmd = std::string("g++ -std=c++23 -I") + tmp.string() +
 " " + tu.string() + " -o " + (tmp / "a.out").string();
#endif
 auto compileOut = runCommand(compileCmd);
 INFO(compileOut);

 // If compile produced an executable, consider success
#ifdef _MSC_VER
 REQUIRE(fs::exists(tmp / "a.exe"));
#else
 REQUIRE(fs::exists(tmp / "a.out"));
#endif
}
