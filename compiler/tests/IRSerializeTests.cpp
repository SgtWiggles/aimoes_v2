#include <catch2/catch_all.hpp>

#include <vector>

#include "ao/pack/ByteStream.h"
#include "ao/schema/IR.h"
#include "ao/schema/Serializer.h"

using namespace ao::pack::byte;
using namespace ao::schema;
using namespace ao::schema::ir;

template <class T>
T roundtrip(T const& in) {
    // sizing pass
    SizeWriteStream sizer;
    ao::schema::serialize(sizer, in);
    REQUIRE(sizer.ok());
    auto size = sizer.byteSize();

    std::vector<std::byte> buf(size);
    if (size > 0) {
        WriteStream writer(std::span{buf.data(), buf.size()});
        ao::schema::serialize(writer, in);
        REQUIRE(writer.ok());
    }

    ReadStream reader(size ? std::span{buf.data(), buf.size()}
                           : std::span<std::byte const>{});
    T out{};
    ao::schema::deserialize(reader, out);
    REQUIRE(reader.ok());
    return out;
}

TEST_CASE("Serialize/Deserialize DirectiveValue and DirectiveProperty",
          "[serialize]") {
    DirectiveValue dv;
    dv.value = IdFor<std::string>{0};

    auto dv2 = roundtrip(dv);
    REQUIRE(std::holds_alternative<IdFor<std::string>>(dv2.value));
    REQUIRE(std::get<IdFor<std::string>>(dv2.value).idx == 0);

    DirectiveProperty prop;
    prop.name = IdFor<std::string>{1};
    prop.value = dv;

    auto prop2 = roundtrip(prop);
    REQUIRE(prop2.name.idx == 1);
    REQUIRE(std::get<IdFor<std::string>>(prop2.value.value).idx == 0);
}

TEST_CASE("Serialize/Deserialize DirectiveProfile and DirectiveSet",
          "[serialize]") {
    DirectiveProfile prof;
    prof.domain = DirectiveProfile::Custom;
    prof.profileName = IdFor<std::string>{2};
    prof.properties.push_back(IdFor<DirectiveProperty>{0});

    auto prof2 = roundtrip(prof);
    REQUIRE(prof2.domain == prof.domain);
    REQUIRE(prof2.profileName.idx == 2);
    REQUIRE(prof2.properties.size() == 1);
    REQUIRE(prof2.properties[0].idx == 0);

    DirectiveSet ds;
    ds.directives.push_back(IdFor<DirectiveProfile>{0});
    auto ds2 = roundtrip(ds);
    REQUIRE(ds2.directives.size() == 1);
    REQUIRE(ds2.directives[0].idx == 0);
}

TEST_CASE("Serialize/Deserialize Field, OneOf and Message", "[serialize]") {
    Field f;
    f.name = IdFor<std::string>{3};
    f.fieldNumber = 7;
    f.type = IdFor<Type>{1};
    f.directives = IdFor<DirectiveSet>{0};

    auto f2 = roundtrip(f);
    REQUIRE(f2.name.idx == 3);
    REQUIRE(f2.fieldNumber == 7);
    REQUIRE(f2.type.idx == 1);
    REQUIRE(f2.directives.idx == 0);

    OneOf one;
    one.arms.push_back(IdFor<Field>{0});
    auto one2 = roundtrip(one);
    REQUIRE(one2.arms.size() == 1);
    REQUIRE(one2.arms[0].idx == 0);

    Message m;
    m.name = IdFor<std::string>{4};
    m.symbolId = 12345;
    m.messageNumber = std::optional<uint64_t>(99);
    m.fields.push_back(IdFor<Field>{0});
    m.directives = IdFor<DirectiveSet>{0};

    auto m2 = roundtrip(m);
    // Message's operator<=> only compares symbolId, but we check members
    // directly
    REQUIRE(m2.symbolId == 12345);
    REQUIRE(m2.name.idx == 4);
    REQUIRE(m2.messageNumber.has_value());
    REQUIRE(m2.messageNumber.value() == 99);
    REQUIRE(m2.fields.size() == 1);
    REQUIRE(m2.fields[0].idx == 0);
    REQUIRE(m2.directives.idx == 0);
}

TEST_CASE("Serialize/Deserialize Scalar, Array, Optional and Type",
          "[serialize]") {
    Scalar s;
    s.kind = Scalar::INT;
    s.width = 32;
    auto s2 = roundtrip(s);
    REQUIRE(s2.kind == s.kind);
    REQUIRE(s2.width == s.width);

    Array a;
    a.type = IdFor<Type>{2};
    a.minSize = std::optional<uint64_t>(1);
    a.maxSize = std::optional<uint64_t>(10);
    auto a2 = roundtrip(a);
    REQUIRE(a2.type.idx == 2);
    REQUIRE(a2.minSize.has_value());
    REQUIRE(a2.maxSize.has_value());
    REQUIRE(a2.minSize.value() == 1);
    REQUIRE(a2.maxSize.value() == 10);

    Optional opt;
    opt.type = IdFor<Type>{3};
    auto opt2 = roundtrip(opt);
    REQUIRE(opt2.type.idx == 3);

    Type t1;
    t1.payload = s;  // scalar payload
    auto t12 = roundtrip(t1);
    // ensure payload is Scalar and width preserved
    if (auto p = std::get_if<Scalar>(&t12.payload)) {
        REQUIRE(p->kind == Scalar::INT);
        REQUIRE(p->width == 32);
    } else {
        FAIL("expected Scalar payload");
    }

    Type t2;
    t2.payload = IdFor<Message>{0};
    auto t22 = roundtrip(t2);
    if (auto p = std::get_if<IdFor<Message>>(&t22.payload)) {
        REQUIRE(p->idx == 0);
    } else {
        FAIL("expected IdFor<Message> payload");
    }
}

TEST_CASE("Serialize/Deserialize Module and IR", "[serialize]") {
    Module mod;
    mod.moduleName = IdFor<std::string>{5};
    mod.messages.push_back(IdFor<Message>{0});

    auto mod2 = roundtrip(mod);
    REQUIRE(mod2.moduleName.idx == 5);
    REQUIRE(mod2.messages.size() == 1);
    REQUIRE(mod2.messages[0].idx == 0);

    IR ir;
    ir.strings = {"a", "b", "c"};
    // directiveProperties
    DirectiveProperty dp;
    dp.name = IdFor<std::string>{0};
    dp.value.value = true;
    ir.directiveProperties.push_back(dp);

    DirectiveProfile dprof;
    dprof.domain = DirectiveProfile::Disk;
    dprof.profileName = IdFor<std::string>{1};
    dprof.properties.push_back(IdFor<DirectiveProperty>{0});
    ir.directiveProfiles.push_back(dprof);

    DirectiveSet dset;
    dset.directives.push_back(IdFor<DirectiveProfile>{0});
    ir.directiveSets.push_back(dset);

    OneOf one;
    one.arms.push_back(IdFor<Field>{0});
    ir.oneOfs.push_back(one);

    Field f;
    f.name = IdFor<std::string>{2};
    f.fieldNumber = 42;
    f.type = IdFor<Type>{0};
    f.directives = IdFor<DirectiveSet>{0};
    ir.fields.push_back(f);

    Message m;
    m.name = IdFor<std::string>{1};
    m.symbolId = 7;
    m.messageNumber = std::optional<uint64_t>(1);
    m.fields.push_back(IdFor<Field>{0});
    m.directives = IdFor<DirectiveSet>{0};
    ir.messages.push_back(m);

    Type t;
    t.payload = Scalar{.kind = Scalar::UINT, .width = 8};
    ir.types.push_back(t);

    ir.modules.push_back(mod);

    auto ir2 = roundtrip(ir);
    REQUIRE(ir2.strings.size() == ir.strings.size());
    REQUIRE(ir2.directiveProperties.size() == 1);
    REQUIRE(ir2.directiveProfiles.size() == 1);
    REQUIRE(ir2.directiveSets.size() == 1);
    REQUIRE(ir2.oneOfs.size() == 1);
    REQUIRE(ir2.fields.size() == 1);
    REQUIRE(ir2.messages.size() == 1);
    REQUIRE(ir2.types.size() == 1);
    REQUIRE(ir2.modules.size() == 1);

    // spot-check some values
    REQUIRE(ir2.strings[0] == "a");
    REQUIRE(ir2.fields[0].fieldNumber == 42);
    REQUIRE(ir2.messages[0].symbolId == 7);
    if (auto p = std::get_if<Scalar>(&ir2.types[0].payload)) {
        REQUIRE(p->kind == Scalar::UINT);
        REQUIRE(p->width == 8);
    } else {
        FAIL("expected scalar type in ir.types[0]");
    }
}
