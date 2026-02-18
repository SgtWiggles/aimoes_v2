#include "Helpers.h"

#include "ao/schema/Parser.h"

using namespace ao::schema;

AstQualifiedName qnameFromString(std::string const& s) {
    AstQualifiedName q;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '.')) {
        if (!part.empty())
            q.name.push_back(part);
    }
    return q;
}

SourceLocation locFor(std::string const& path) {
    return SourceLocation{path, 1, 1};
}

std::shared_ptr<AstFile> makeFileWithPackageAndDecls(
    std::string absolutePath,
    std::optional<std::string> packageName,
    std::vector<AstDecl> decls,
    std::vector<std::string> imports) {
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

    // Insert imports first
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

AstMessage makeMessage(std::string const& name,
                       std::vector<AstFieldDecl> fields,
                       std::optional<uint64_t> messageId) {
    AstMessage m;
    m.name = name;
    m.block.fields = std::move(fields);
    m.loc = {};
    m.messageId = messageId;
    return m;
}

AstFieldDecl makeFieldDecl(AstField field) {
    AstFieldDecl d;
    d.field = std::move(field);
    d.loc = field.loc;
    return d;
}

AstFieldDecl makeFieldDeclReserved(std::vector<uint64_t> reservedIds) {
    AstFieldDecl d;
    AstFieldReserved r;
    r.fieldNumbers = std::move(reservedIds);
    r.loc = {};
    d.field = std::move(r);
    d.loc = {};
    return d;
}

AstField makeField(std::string const& name, uint64_t number, AstType type) {
    AstField f;
    f.name = name;
    f.fieldNumber = number;
    f.typeName = std::move(type);
    f.loc = {};
    return f;
}

AstFieldReserved makeReserved(std::vector<uint64_t> ids) {
    AstFieldReserved r;
    r.fieldNumbers = std::move(ids);
    r.loc = {};
    return r;
}

AstType makeUserType(std::string const& qualifiedName,
                     std::vector<std::shared_ptr<AstType>> subtypes) {
    AstType t;
    t.type = AstBaseType::USER;
    t.name = qnameFromString(qualifiedName);
    t.subtypes = std::move(subtypes);
    t.loc = {};
    return t;
}

AstType makeCtorType(AstBaseType base,
                     std::vector<std::shared_ptr<AstType>> subtypes) {
    AstType t;
    t.type = base;
    t.subtypes = std::move(subtypes);
    t.loc = {};
    return t;
}

// Directive helpers ----------------------------------------------------------

AstValueLiteral makeStrLit(std::string const& s) {
    AstValueLiteral lit;
    lit.type = ValueLiteralType::STRING;
    lit.contents = s;
    lit.loc = {};
    return lit;
}

AstDirective makeDirective(
    std::string const& directiveName,
    std::vector<std::pair<std::string, std::string>> properties) {
    AstDirective d;
    d.type = AstFieldDirectiveType::CUSTOM;
    d.directiveName = directiveName;
    d.properties.clear();
    for (auto const& p : properties) {
        d.properties[p.first] = makeStrLit(p.second);
    }
    d.loc = {};
    return d;
}

AstDirectiveBlock makeDirectiveBlock(std::vector<AstDirective> directives) {
    AstDirectiveBlock b;
    b.directives = std::move(directives);
    b.effectiveDirectives.clear();
    return b;
}

AstDecl makeDefaultDeclWithDirectiveBlock(AstDirectiveBlock block) {
    AstDecl decl;
    AstDefault def;
    def.directives = std::move(block);
    def.loc = {};
    decl.decl = def;
    decl.loc = {};
    return decl;
}

class TestTextFrontend : public CompilerFrontend {
   public:
    TestTextFrontend(std::unordered_map<std::string, std::string> files)
        : m_files(std::move(files)) {}
    std::expected<std::shared_ptr<AstFile>, std::string> loadFile(
        std::string resolvedPath) override {
        auto iter = m_files.find(resolvedPath);
        if (iter == m_files.end())
            return std::unexpected("Failed to find file");
        std::string parseErrors;
        auto ast = parseToAst(resolvedPath, iter->second, &parseErrors);
        if (!ast)
            return std::unexpected(parseErrors);
        return ast;
    }
    std::expected<std::string, std::string> resolvePath(
        std::string currentFile,
        std::string path) override {
        return path;
    }

   private:
    std::unordered_map<std::string, std::string> m_files;
};

std::optional<ao::schema::ir::IR> buildToIR(std::string_view fileContents,
                                            std::string& errs) {
    TestTextFrontend fe{{{"file", std::string{fileContents}}}};
    SemanticContext ctx{fe};
    auto success = ctx.loadFile("file") && ctx.validate();
    if (!success) {
        errs = ctx.getErrorContext().toString();
        return {};
    }
    auto ir =
        ao::schema::ir::generateIR(ctx.getModules(), ctx.getErrorContext());
    if (ctx.getErrorContext().errors.size() > 0) {
        errs = ctx.getErrorContext().toString();
        return {};
    }
    return ir;
}

// SimpleTestFrontend method implementations ----------------------------------

std::expected<std::string, std::string> SimpleTestFrontend::resolvePath(
    std::string /*currentFile*/,
    std::string path) {
    if (resolvedModules.find(path) != resolvedModules.end())
        return path;
    return std::unexpected<std::string>("could not resolve: " + path);
}

std::expected<std::shared_ptr<AstFile>, std::string>
SimpleTestFrontend::loadFile(std::string resolvedPath) {
    auto it = resolvedModules.find(resolvedPath);
    if (it == resolvedModules.end())
        return std::unexpected<std::string>("file not found: " + resolvedPath);
    return it->second;
}
