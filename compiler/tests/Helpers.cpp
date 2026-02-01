#include "Helpers.h"

using namespace ao::schema;

ao::schema::AstQualifiedName qnameFromString(std::string const& s) {
    ao::schema::AstQualifiedName q;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '.')) {
        if (!part.empty())
            q.name.push_back(part);
    }
    return q;
}

ao::schema::SourceLocation locFor(std::string const& path) {
    return ao::schema::SourceLocation{path, 1, 1};
}

std::shared_ptr<ao::schema::AstFile> makeFileWithPackageAndDecls(
    std::string absolutePath,
    std::optional<std::string> packageName,
    std::vector<ao::schema::AstDecl> decls,
    std::vector<std::string> imports) {
    auto f = std::make_shared<ao::schema::AstFile>();
    f->absolutePath = absolutePath;
    f->loc = locFor(absolutePath);

    // Insert package decl if present
    if (packageName.has_value()) {
        ao::schema::AstDecl pd;
        ao::schema::AstPackageDecl pdDecl;
        pdDecl.name = qnameFromString(*packageName);
        pdDecl.loc = locFor(absolutePath);
        pd.decl = pdDecl;
        pd.loc = pdDecl.loc;
        f->decls.push_back(std::move(pd));
    }

    // Insert imports first (so tests can control ordering if needed)
    for (auto const& imp : imports) {
        ao::schema::AstDecl id;
        ao::schema::AstImport ai;
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

ao::schema::AstMessage makeMessage(
    std::string const& name,
    std::vector<ao::schema::AstFieldDecl> fields) {
    ao::schema::AstMessage m;
    m.name = name;
    m.block.fields = std::move(fields);
    m.loc = {};  // tests don't rely on exact loc
    return m;
}

ao::schema::AstFieldDecl makeFieldDecl(ao::schema::AstField field) {
    ao::schema::AstFieldDecl d;
    d.field = std::move(field);
    d.loc = field.loc;
    return d;
}

ao::schema::AstField makeField(std::string const& name,
                               uint64_t number,
                               ao::schema::AstTypeName type) {
    ao::schema::AstField f;
    f.name = name;
    f.fieldNumber = number;
    f.typeName = std::move(type);
    f.loc = {};  // not used by these tests
    return f;
}

ao::schema::AstTypeName makeUserType(
    std::string const& qualifiedName,
    std::vector<std::shared_ptr<ao::schema::AstTypeName>> subtypes) {
    ao::schema::AstTypeName t;
    t.type = ao::schema::AstBaseType::USER;
    t.name = qnameFromString(qualifiedName);
    t.subtypes = std::move(subtypes);
    t.loc = {};
    return t;
}

ao::schema::AstTypeName makeCtorType(
    ao::schema::AstBaseType base,
    std::vector<std::shared_ptr<ao::schema::AstTypeName>> subtypes) {
    ao::schema::AstTypeName t;
    t.type = base;
    t.subtypes = std::move(subtypes);
    t.loc = {};
    return t;
}

// SimpleTestFrontend method implementations ----------------------------------

std::expected<std::string, std::string> SimpleTestFrontend::resolvePath(
    std::string /*currentFile*/,
    std::string path) {
    if (resolvedModules.find(path) != resolvedModules.end())
        return path;
    return std::unexpected<std::string>("could not resolve: " + path);
}

std::expected<std::shared_ptr<ao::schema::AstFile>, std::string>
SimpleTestFrontend::loadFile(std::string resolvedPath) {
    auto it = resolvedModules.find(resolvedPath);
    if (it == resolvedModules.end())
        return std::unexpected<std::string>("file not found: " + resolvedPath);
    return it->second;
}