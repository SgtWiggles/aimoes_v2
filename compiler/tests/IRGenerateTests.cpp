#include <catch2/catch_all.hpp>

#include "ao/schema/IR.h"
#include "ao/schema/Parser.h"
#include "ao/schema/SemanticContext.h"

#include "Helpers.h"

using namespace ao::schema;
using namespace ao::schema::ir;

static std::optional<uint64_t> findStringIndex(IR const& ir,
                                               std::string const& s) {
    auto iter = std::find(ir.strings.begin(), ir.strings.end(), s);
    if (iter == ir.strings.end())
        return std::nullopt;
    return std::distance(ir.strings.begin(), iter);
}
static std::optional<Field> findFieldByName(IR const& ir,
                                            Message const& m,
                                            std::string const& name) {
    for (auto fid : m.fields) {
        auto const& f = ir.fields[fid.idx];
        if (ir.strings[f.name.idx] == name)
            return f;
    }
    return std::nullopt;
}

TEST_CASE("generateIR retains types, messages, fields and directives", "[ir]") {
    SimpleTestFrontend frontend;

    // Create a module-level default directive (should flow to fields unless
    // overridden)
    AstDirective defaultDir = makeDirective("prof", {{"tag", "global"}});
    AstDirectiveBlock defaultBlock = makeDirectiveBlock({defaultDir});
    AstDecl defaultDecl = makeDefaultDeclWithDirectiveBlock(defaultBlock);

    // Message A with many field kinds
    // Fields:
    // 1 f int;
    // 2 g string;
    // 3 h array<int>;
    // 4 i optional<int>;
    // 5 j oneof { 1 x int; 2 y string; };
    AstField f1 = makeField("f", 1, makeCtorType(AstBaseType::INT));
    AstField f2 = makeField("g", 2, makeCtorType(AstBaseType::STRING));
    AstField f3 = makeField(
        "h", 3,
        makeCtorType(AstBaseType::ARRAY, {std::make_shared<AstType>(
                                             makeCtorType(AstBaseType::INT))}));
    AstField f4 = makeField(
        "i", 4,
        makeCtorType(
            AstBaseType::OPTIONAL,
            {std::make_shared<AstType>(makeCtorType(AstBaseType::INT))}));

    // oneof type: construct the AstType and block entries directly
    AstType oneofType = makeCtorType(AstBaseType::ONEOF);
    // add arms to oneof block
    AstField oneA = makeField("x", 1, makeCtorType(AstBaseType::INT));
    AstField oneB = makeField("y", 2, makeCtorType(AstBaseType::STRING));
    oneofType.block.fields.push_back(makeFieldDecl(oneA));
    oneofType.block.fields.push_back(makeFieldDecl(oneB));
    AstField f5 = makeField("j", 5, oneofType);

    // Message A
    AstMessage msgA =
        makeMessage("A",
                    {makeFieldDecl(f1), makeFieldDecl(f2), makeFieldDecl(f3),
                     makeFieldDecl(f4), makeFieldDecl(f5)},
                    42);

    AstDecl declA;
    declA.decl = msgA;

    // Message B that references A (USER type)
    AstType userA = makeUserType("A");
    AstField b1 = makeField("refA", 1, userA);
    AstMessage msgB = makeMessage("B", {makeFieldDecl(b1)}, 43);
    AstDecl declB;
    declB.decl = msgB;

    // Create a field-level directive on B.refA to override default
    AstDirective fldDir = makeDirective("prof", {{"tag", "fieldVal"}});
    AstDirectiveBlock fldBlock = makeDirectiveBlock({fldDir});
    // inject into the AST field (we built earlier), need to attach in msgB AST
    // Recreate field with directives
    AstField b1_withdir = makeField("refA", 1, userA);
    b1_withdir.directives = fldBlock;
    msgB.block.fields.clear();
    msgB.block.fields.push_back(makeFieldDecl(b1_withdir));
    declB.decl = msgB;

    // Build file with package and declarations (default then A then B)
    auto file =
        makeFileWithPackageAndDecls("modA", "pkg", {defaultDecl, declA, declB});
    frontend.resolvedModules["modA"] = file;

    // Semantic pipeline
    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("modA") == true);
    REQUIRE(ctx.validate() == true);

    auto const& modules = ctx.getModules();

    // Generate IR
    ErrorContext irErrors;
    auto ir = ao::schema::ir::generateIR(modules, irErrors);
    REQUIRE(irErrors.errors.size() == 0);

    // Basic expectations
    // - Strings should contain qualified message names "pkg.A" and "pkg.B"
    auto idxA = findStringIndex(ir, "pkg.A");
    auto idxB = findStringIndex(ir, "pkg.B");
    REQUIRE(idxA.has_value());
    REQUIRE(idxB.has_value());

    // Find the IR Message entries by matching message.name -> string
    std::optional<Message> irA;
    std::optional<Message> irB;
    for (auto const& m : ir.messages) {
        auto s = ir.strings[m.name.idx];
        if (s == "pkg.A")
            irA = m;
        if (s == "pkg.B")
            irB = m;
    }
    REQUIRE(irA.has_value());
    REQUIRE(irB.has_value());

    // Message A should carry the original messageNumber
    REQUIRE(irA->messageNumber.has_value());
    CHECK(irA->messageNumber.value() == 42);

    // Inspect fields of A by looking up the names in strings and matching in
    // ir.fields
    auto findFieldInIR =
        [&](Message const& m,
            std::string const& fieldName) -> std::optional<Field> {
        for (auto fid : m.fields) {
            auto const& f = ir.fields[fid.idx];
            if (ir.strings[f.name.idx] == fieldName)
                return f;
        }
        return std::nullopt;
    };

    auto f_ir_f = findFieldInIR(*irA, "f");
    auto f_ir_g = findFieldInIR(*irA, "g");
    auto f_ir_h = findFieldInIR(*irA, "h");
    auto f_ir_i = findFieldInIR(*irA, "i");
    auto f_ir_j = findFieldInIR(*irA, "j");

    REQUIRE(f_ir_f.has_value());
    REQUIRE(f_ir_g.has_value());
    REQUIRE(f_ir_h.has_value());
    REQUIRE(f_ir_i.has_value());
    REQUIRE(f_ir_j.has_value());

    // Check field numbers
    CHECK(f_ir_f->fieldNumber == 1);
    CHECK(f_ir_g->fieldNumber == 2);
    CHECK(f_ir_h->fieldNumber == 3);
    CHECK(f_ir_i->fieldNumber == 4);
    CHECK(f_ir_j->fieldNumber == 5);

    // Helper to resolve Type payload kinds
    auto isScalarKind = [&](IdFor<Type> typeId,
                            Scalar::ScalarKind kind) -> bool {
        auto const& t = ir.types[typeId.idx];
        if (auto p = std::get_if<Scalar>(&t.payload)) {
            return p->kind == kind;
        }
        return false;
    };
    auto isArrayOfUint8 = [&](IdFor<Type> typeId) -> bool {
        auto const& t = ir.types[typeId.idx];
        if (auto p = std::get_if<Array>(&t.payload)) {
            auto inner = ir.types[p->type.idx];
            if (auto innerScalar = std::get_if<Scalar>(&inner.payload)) {
                return innerScalar->kind == Scalar::UINT &&
                       innerScalar->width == 8;
            }
        }
        return false;
    };
    auto isOptionalOfInt = [&](IdFor<Type> typeId) -> bool {
        auto const& t = ir.types[typeId.idx];
        if (auto p = std::get_if<Optional>(&t.payload)) {
            return isScalarKind(p->type, Scalar::INT);
        }
        return false;
    };
    auto isOneOf = [&](IdFor<Type> typeId) -> std::optional<IdFor<OneOf>> {
        auto const& t = ir.types[typeId.idx];
        if (auto p = std::get_if<IdFor<OneOf>>(&t.payload)) {
            return *p;
        }
        return std::nullopt;
    };

    // f -> int (scalar)
    CHECK(isScalarKind(f_ir_f->type, Scalar::INT));

    // g -> string -> represented as array<uint8_t>
    CHECK(isArrayOfUint8(f_ir_g->type));

    // h -> array<int> -> Array whose inner is Scalar INT
    {
        auto const& t = ir.types[f_ir_h->type.idx];
        auto p = std::get_if<Array>(&t.payload);
        REQUIRE(p != nullptr);
        CHECK(isScalarKind(p->type, Scalar::INT));
    }

    // i -> optional<int>
    CHECK(isOptionalOfInt(f_ir_i->type));

    // j -> oneof
    {
        auto maybeOneOfId = isOneOf(f_ir_j->type);
        REQUIRE(maybeOneOfId.has_value());
        auto const& oneof = ir.oneOfs[maybeOneOfId->idx];
        // two arms
        CHECK(oneof.arms.size() == 2);
        // arms should reference fields (x and y) created within the oneof block
        // Validate their types: first int, second string (array<uint8_t>)
        auto arm0 = ir.fields[oneof.arms[0].idx];
        auto arm1 = ir.fields[oneof.arms[1].idx];
        CHECK(isScalarKind(arm0.type, Scalar::INT));
        CHECK(isArrayOfUint8(arm1.type));
    }

    // Now verify USER type mapping: B.refA should be a Type that points to
    // Message A find refA in B
    auto refA_ir = findFieldInIR(*irB, "refA");
    REQUIRE(refA_ir.has_value());
    {
        auto const& t = ir.types[refA_ir->type.idx];
        auto p = std::get_if<IdFor<Message>>(&t.payload);
        REQUIRE(p != nullptr);
        // the message id should point to message A (compare symbolId)
        auto const& referencedMsg = ir.messages[p->idx];
        CHECK(referencedMsg.messageNumber.has_value());
        CHECK(referencedMsg.messageNumber.value() == 42);
        // also the referenced message qualified name string must be "pkg.A"
        CHECK(ir.strings[referencedMsg.name.idx] == "pkg.A");
    }

    // Directives: B.refA had its own directive overriding the default
    // Inspect the directive set for refA_ir
    {
        auto const& fieldDirSet = ir.directiveSets[refA_ir->directives.idx];
        // should contain at least one profile (our "prof")
        REQUIRE(!fieldDirSet.directives.empty());
        // find profile "prof" by looking up profileName string
        bool foundProf = false;
        for (auto dpid : fieldDirSet.directives) {
            auto const& profile = ir.directiveProfiles[dpid.idx];
            // for custom profiles profileName is stored
            auto pname = ir.strings[profile.profileName.idx];
            if (pname == "prof") {
                foundProf = true;
                // profile should have one property (tag="fieldVal")
                REQUIRE(profile.properties.size() == 1);
                auto propId = profile.properties[0];
                auto const& prop = ir.directiveProperties[propId.idx];
                // property name should be "tag"
                CHECK(ir.strings[prop.name.idx] == "tag");
                // value should be IdFor<std::string> with contents "fieldVal"
                auto const& val = prop.value.value;
                // value is
                // variant<bool,double,int64_t,uint64_t,IdFor<std::string>>
                if (auto pstr = std::get_if<IdFor<std::string>>(&val)) {
                    CHECK(ir.strings[pstr->idx] == "fieldVal");
                } else {
                    FAIL("expected string literal for directive property");
                }
            }
        }
        CHECK(foundProf);
    }
}

TEST_CASE("generateIR from text: scalar widths, arrays and user refs",
          "[ir][text]") {
    std::string errs;
    auto ast = ao::schema::parseToAst("modA", R"(
package pkg;
default @prof(tag="global");

message 42 A {
    1 a int(bits=16);
    2 b uint(bits=32);
    3 c string;
    4 d array<int>;
}

message 43 B {
    1 refA A @prof(tag="fieldVal");
}
)",
                                      &errs);

    INFO(errs);
    REQUIRE(ast != nullptr);

    SimpleTestFrontend frontend;
    frontend.resolvedModules["modA"] = ast;

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("modA") == true);
    auto validated = ctx.validate();
    INFO(ctx.getErrorContext().toString());
    REQUIRE(validated == true);

    auto const& modules = ctx.getModules();

    ErrorContext irErrs;
    auto ir = ao::schema::ir::generateIR(modules, irErrs);
    REQUIRE(irErrs.errors.empty());

    // find messages
    auto idxA = findStringIndex(ir, "pkg.A");
    auto idxB = findStringIndex(ir, "pkg.B");
    REQUIRE(idxA.has_value());
    REQUIRE(idxB.has_value());

    std::optional<Message> mA, mB;
    for (auto const& m : ir.messages) {
        if (ir.strings[m.name.idx] == "pkg.A")
            mA = m;
        if (ir.strings[m.name.idx] == "pkg.B")
            mB = m;
    }
    REQUIRE(mA.has_value());
    REQUIRE(mB.has_value());

    // check fields exist
    auto fa = findFieldByName(ir, *mA, "a");
    auto fb = findFieldByName(ir, *mA, "b");
    auto fc = findFieldByName(ir, *mA, "c");
    auto fd = findFieldByName(ir, *mA, "d");
    REQUIRE(fa.has_value());
    REQUIRE(fb.has_value());
    REQUIRE(fc.has_value());
    REQUIRE(fd.has_value());

    // helper to read scalar type
    auto asScalar = [&](IdFor<Type> id) -> std::optional<Scalar> {
        auto const& t = ir.types[id.idx];
        if (auto p = std::get_if<Scalar>(&t.payload))
            return *p;
        return std::nullopt;
    };

    // a -> int(bits=16)
    {
        auto s = asScalar(fa->type);
        REQUIRE(s.has_value());
        CHECK(s->kind == Scalar::INT);
        CHECK(s->width == 16);
    }

    // b -> uint(bits=32)
    {
        auto s = asScalar(fb->type);
        REQUIRE(s.has_value());
        CHECK(s->kind == Scalar::UINT);
        CHECK(s->width == 32);
    }

    // c -> string -> represented as Array whose inner is uint8
    {
        auto const& t = ir.types[fc->type.idx];
        auto parr = std::get_if<Array>(&t.payload);
        REQUIRE(parr != nullptr);
        auto inner = ir.types[parr->type.idx];
        auto innerScalar = std::get_if<Scalar>(&inner.payload);
        REQUIRE(innerScalar != nullptr);
        CHECK(innerScalar->kind == Scalar::UINT);
        CHECK(innerScalar->width == 8);
    }

    // d -> array<int>
    {
        auto const& t = ir.types[fd->type.idx];
        auto parr = std::get_if<Array>(&t.payload);
        REQUIRE(parr != nullptr);
        auto inner = ir.types[parr->type.idx];
        auto innerScalar = std::get_if<Scalar>(&inner.payload);
        REQUIRE(innerScalar != nullptr);
        CHECK(innerScalar->kind == Scalar::INT);
    }

    // B.refA should be USER -> IdFor<Message> payload referring to A
    auto refA = findFieldByName(ir, *mB, "refA");
    REQUIRE(refA.has_value());
    {
        auto const& t = ir.types[refA->type.idx];
        auto pmsg = std::get_if<IdFor<Message>>(&t.payload);
        REQUIRE(pmsg != nullptr);
        auto const& referenced = ir.messages[pmsg->idx];
        CHECK(ir.strings[referenced.name.idx] == "pkg.A");
        CHECK(referenced.messageNumber.has_value());
        CHECK(referenced.messageNumber.value() == 42);
    }

    // Directives: B.refA has explicit field-level @prof(tag="fieldVal")
    {
        auto const& ds = ir.directiveSets[refA->directives.idx];
        bool foundProf = false;
        for (auto pid : ds.directives) {
            auto const& profile = ir.directiveProfiles[pid.idx];
            auto pname = ir.strings[profile.profileName.idx];
            if (pname == "prof") {
                foundProf = true;
                REQUIRE(profile.properties.size() == 1);
                auto prop = ir.directiveProperties[profile.properties[0].idx];
                CHECK(ir.strings[prop.name.idx] == "tag");
                auto const& val = prop.value.value;
                if (auto pstr = std::get_if<IdFor<std::string>>(&val)) {
                    CHECK(ir.strings[pstr->idx] == "fieldVal");
                } else {
                    FAIL("expected string value for directive property");
                }
            }
        }
        CHECK(foundProf);
    }
}

TEST_CASE(
    "generateIR from text: nested optional + oneof and directive propagation",
    "[ir][text]") {
    std::string errs;
    auto ast = ao::schema::parseToAst("modB", R"(
package pkg;
default @disk(enabled=true);

message 100 Inner  {
    1 xi int;
    2 xs string;
}

message 101 Outer  {
    1 opt optional<oneof {
        1 a int;
        2 b string;
    }>;
    2 arr array<string>;
    3 withDefault int;
}
)",
                                      &errs);

    REQUIRE(ast != nullptr);

    SimpleTestFrontend frontend;
    frontend.resolvedModules["modB"] = ast;

    SemanticContext ctx{frontend};
    REQUIRE(ctx.loadFile("modB") == true);
    auto validated = ctx.validate();
    INFO(ctx.getErrorContext().toString());
    REQUIRE(validated == true);

    auto const& modules = ctx.getModules();

    ErrorContext irErrs;
    auto ir = ao::schema::ir::generateIR(modules, irErrs);
    REQUIRE(irErrs.errors.empty());

    // find Outer
    std::optional<Message> outer;
    for (auto const& m : ir.messages) {
        if (ir.strings[m.name.idx] == "pkg.Outer")
            outer = m;
    }
    REQUIRE(outer.has_value());

    // find field 'opt' and ensure it's an Optional whose inner is a OneOf
    auto fopt = findFieldByName(ir, outer.value(), "opt");
    REQUIRE(fopt.has_value());
    {
        auto const& topt = ir.types[fopt->type.idx];
        auto popt = std::get_if<Optional>(&topt.payload);
        REQUIRE(popt != nullptr);

        // inner should be a OneOf id
        auto const& innerType = ir.types[popt->type.idx];
        auto poneof = std::get_if<IdFor<OneOf>>(&innerType.payload);
        REQUIRE(poneof != nullptr);
        auto const& oneof = ir.oneOfs[poneof->idx];
        CHECK(oneof.arms.size() == 2);

        // arm 0 -> int, arm 1 -> string (array<uint8_t>)
        auto arm0 = ir.fields[oneof.arms[0].idx];
        auto arm1 = ir.fields[oneof.arms[1].idx];
        auto s0 = std::get_if<Scalar>(&ir.types[arm0.type.idx].payload);
        REQUIRE(s0 != nullptr);
        CHECK(s0->kind == Scalar::INT);

        auto parr = std::get_if<Array>(&ir.types[arm1.type.idx].payload);
        REQUIRE(parr != nullptr);
        auto inner = std::get_if<Scalar>(&ir.types[parr->type.idx].payload);
        REQUIRE(inner != nullptr);
        CHECK(inner->kind == Scalar::UINT);
        CHECK(inner->width == 8);
    }

    // Check that default @disk(enabled=true) produced a directive profile on
    // field 'withDefault'
    auto fwith = findFieldByName(ir, *outer, "withDefault");
    REQUIRE(fwith.has_value());
    {
        auto const& ds = ir.directiveSets[fwith->directives.idx];
        bool foundDisk = false;
        for (auto pid : ds.directives) {
            auto const& prof = ir.directiveProfiles[pid.idx];
            if ((int)prof.domain == DirectiveProfile::Disk) {
                foundDisk = true;
                REQUIRE(prof.properties.size() == 1);
                auto const& prop =
                    ir.directiveProperties[prof.properties[0].idx];
                CHECK(ir.strings[prop.name.idx] == "enabled");
                // boolean literal expected
                auto const& val = prop.value.value;
                if (auto pb = std::get_if<bool>(&val)) {
                    CHECK(*pb == true);
                } else {
                    FAIL("expected boolean literal for disk.enabled");
                }
            }
        }
        CHECK(foundDisk);
    }
}