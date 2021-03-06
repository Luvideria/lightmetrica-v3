/*
    Lightmetrica - Copyright (c) 2019 Hisanari Otsu
    Distributed under MIT license. See LICENSE file for details.
*/

#include <pch.h>
#include "test_common.h"
#include <lm/assetgroup.h>

LM_NAMESPACE_BEGIN(LM_TEST_NAMESPACE)

// ------------------------------------------------------------------------------------------------

struct TestAsset : public lm::Component {
    virtual int f() const = 0;
};

struct TestAsset_Simple final : public TestAsset {
    int v = -1;

    virtual void construct(const lm::Json& prop) override {
        if (prop.count("v")) {
            v = prop["v"];
        }
    }

    virtual int f() const override {
        return v;
    }
};

struct TestAsset_Dependent final : public TestAsset {
    TestAsset* other;

    virtual void construct(const lm::Json& prop) override {
        // In this test an instance of Assets are registered as root component
        // thus we can access the underlying component via lm::comp::get function.
        LM_UNUSED(prop);
        other = lm::comp::get<TestAsset>("$.asset1");
    }

    virtual void foreach_underlying(const ComponentVisitor& visit) override {
        lm::comp::visit(visit, other);
    }

    virtual int f() const override {
        return other->f() + 1;
    }
};

LM_COMP_REG_IMPL(TestAsset_Simple, "testasset::simple");
LM_COMP_REG_IMPL(TestAsset_Dependent, "testasset::dependent");

// ------------------------------------------------------------------------------------------------

TEST_CASE("AssetGroup") {
    lm::log::ScopedInit init;

    auto assets = lm::comp::create<lm::AssetGroup>("asset_group::default", "$");
    REQUIRE(assets);

    // Set assets as a root component
    lm::comp::detail::register_root_comp(assets.get());

    SUBCASE("Load asset without properties") {
        auto result = assets->load_asset("asset1", "testasset::simple", {});
        CHECK(result);
        auto* a = lm::comp::get<TestAsset>("$.asset1");
        REQUIRE(a);
        CHECK(a->f() == -1);
    }

    SUBCASE("Load asset with properties") {
        CHECK(assets->load_asset("asset1", "testasset::simple", { {"v", 42} }));
        auto* a = lm::comp::get<TestAsset>("$.asset1");
        REQUIRE(a);
        CHECK(a->f() == 42);
    }

    SUBCASE("Load asset dependent on an other asset") {
        CHECK(assets->load_asset("asset1", "testasset::simple", { {"v", 42} }));
        CHECK(assets->load_asset("asset2", "testasset::dependent", {}));
        auto* a = lm::comp::get<TestAsset>("$.asset2");
        REQUIRE(a);
        CHECK(a->f() == 43);
    }

    SUBCASE("Replacing assets") {
        {
            // Load initial asset
            CHECK(assets->load_asset("asset1", "testasset::simple", { {"v", 42} }));
            auto* a = lm::comp::get<TestAsset>("$.asset1");
            REQUIRE(a);
            CHECK(a->f() == 42);
        }
        {
            // Load another asset with same name
            CHECK(assets->load_asset("asset1", "testasset::simple", { {"v", 43} }));
            auto* a = lm::comp::get<TestAsset>("$.asset1");
            REQUIRE(a);
            CHECK(a->f() == 43);
        }
    }

    SUBCASE("Replacing dependent assets") {
        {
            CHECK(assets->load_asset("asset1", "testasset::simple", { {"v", 42} }));
            CHECK(assets->load_asset("asset2", "testasset::dependent", {}));
            auto* a = lm::comp::get<TestAsset>("$.asset2");
            REQUIRE(a);
            CHECK(a->f() == 43);
        }
        {
            // Replace asset1 referenced by asset2
            CHECK(assets->load_asset("asset1", "testasset::simple", { {"v", 1} }));
            auto* a = lm::comp::get<TestAsset>("$.asset2");
            REQUIRE(a);
            CHECK(a->f() == 2);
        }
    }
}

LM_NAMESPACE_END(LM_TEST_NAMESPACE)
