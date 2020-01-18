/*
    Lightmetrica - Copyright (c) 2019 Hisanari Otsu
    Distributed under MIT license. See LICENSE file for details.
*/

#include <pch.h>
#include <lm/core.h>
#include <lm/model.h>
#include <lm/objloader.h>
#include <lm/mesh.h>
#include <lm/material.h>
#include <lm/texture.h>
#include <lm/film.h>
#include <lm/light.h>
#include <lm/surface.h>

#define NO_MIXTURE_MATERIAL 0

LM_NAMESPACE_BEGIN(LM_NAMESPACE)

using namespace objloader;
class Mesh_WavefrontObj;

class Model_WavefrontObj final : public Model {
private:
    friend class Mesh_WavefrontObj;

private:
    // Surface geometry
    OBJSurfaceGeometry geo_;

    // Underlying assets
    std::vector<Component::Ptr<Component>> assets_;
    std::unordered_map<std::string, int> assets_map_;

    // Mesh group which assocites a mesh and a material
    struct Group {
        int mesh;
        int material;
        int light;

        template <typename Archive>
        void serialize(Archive& ar) {
            ar(mesh, material, light);
        }
    };
    std::vector<Group> groups_;

public:
    LM_SERIALIZE_IMPL(ar) {
        ar(geo_, groups_, assets_map_, assets_);
    }

    virtual void foreach_underlying(const ComponentVisitor& visit) override {
        for (auto& asset : assets_) {
            comp::visit(visit, asset);
        }
    }

public:
    virtual Component* underlying(const std::string& name) const override {
        return assets_[assets_map_.at(name)].get();
    }

	virtual void construct(const Json& prop) override {
        const std::string path = json::value<std::string>(prop, "path");
        bool result = objloader::load(path, geo_,
            // Process mesh
            [&](const OBJMeshFace& fs, const MTLMatParams& m) -> bool {
                // Create mesh
                const std::string mesh_name = fmt::format("mesh_{}", assets_.size());
                auto mesh = comp::create<Mesh>(
                    "mesh::wavefrontobj", make_loc(mesh_name),
                    json::merge(prop, {
                        {"model_", this},
                        {"fs_", (const OBJMeshFace*)&fs}
                    }
                ));
                if (!mesh) {
                    return false;
                }
                assets_map_[mesh_name] = int(assets_.size());
                assets_.push_back(std::move(mesh));

                // Create area light if Ke > 0
                int light_index = -1;
                if (glm::compMax(m.Ke) > 0_f) {
                    const auto light_impl_name = json::value<std::string>(prop, "light", "light::area");
                    const auto light_name = mesh_name + "_light";
                    auto light = comp::create<Light>(light_impl_name, make_loc(light_name), {
                        {"Ke", m.Ke},
                        {"mesh", make_loc(mesh_name)}
                    });
                    if (!light) {
                        return false;
                    }
                    light_index = int(assets_.size());
                    assets_map_[light_name] = int(assets_.size());
                    assets_.push_back(std::move(light));
                }

                // Create mesh group
                groups_.push_back({ assets_map_[mesh_name], assets_map_[m.name], light_index });

                return true;
            },

            // Process material
            [&](const MTLMatParams& m) -> bool {
                // Load user-specified material if given
                if (const auto it = prop.find("base_material"); it != prop.end()) {
                    auto mat = comp::create<Material>("material::proxy", make_loc(m.name), {
                        {"ref", *it}
                    });
                    if (!mat) {
                        return false;
                    }
                    assets_map_[m.name] = int(assets_.size());
                    assets_.push_back(std::move(mat));
                    return true;
                }

                // Load texture
                std::string mapKd_loc;
                if (!m.mapKd.empty()) {
                    // Use texture_<filename> as an identifier
                    const auto id = "texture_" + fs::path(m.mapKd).stem().string();

                    // Check if already loaded
                    if (auto it = assets_map_.find(id); it == assets_map_.end()) {
                        // If not loaded, load the texture
                        const auto textureAssetName = json::value<std::string>(prop, "texture", "texture::bitmap");
                        auto texture = comp::create<Texture>(textureAssetName, make_loc(id), {
                            {"path", (fs::path(path).remove_filename()/m.mapKd).string()}
                        });
                        if (!texture) {
                            return false;
                        }
                        assets_map_[id] = int(assets_.size());
                        assets_.push_back(std::move(texture));
                    }

                    // Locator of the texture
                    mapKd_loc = make_loc(id);
                }

                // Load material
                Ptr<Material> mat;
                const bool skip_specular_mat = json::value<bool>(prop, "skip_specular_mat", false);
                if (m.illum == 5 || m.illum == 7) {
                    if (skip_specular_mat) {
                        // Skip specular material. Use black material.
                        mat = comp::create<Material>(
                            "material::diffuse", make_loc(m.name), { {"Kd", Vec3(0_f)} });
                    }
                    else if (m.illum == 7) {
                        // Glass
                        mat = comp::create<Material>(
                            "material::glass", make_loc(m.name), { {"Ni", m.Ni} });
                    }
                    else if (m.illum == 5) {
                        // Mirror
                        mat = comp::create<Material>(
                            "material::mirror", make_loc(m.name), {});
                    }
                }
                else {

                    #if NO_MIXTURE_MATERIAL
                    if (math::is_zero(m.Ks)) {
                        // Diffuse material
                        mat = comp::create<Material>(
                            "material::diffuse", make_loc(m.name), {
                                {"Kd", m.Kd},
                                {"mapKd", mapKd_loc}
                            });
                    }
                    else {
                        // Glossy material
                        const auto r = 2_f / (2_f + m.Ns);
                        const auto as = math::safe_sqrt(1_f - m.an * .9_f);
                        mat = comp::create<Material>(
                            "material::glossy", make_loc(m.name), {
                                {"Ks", m.Ks},
                                {"ax", std::max(1e-3_f, r / as)},
                                {"ay", std::max(1e-3_f, r * as)}
                            });
                        
                    }
                    #else
                    // Convert parameter for anisotropic GGX 
                    const auto r = 2_f / (2_f + m.Ns);
                    const auto as = math::safe_sqrt(1_f - m.an * .9_f);

                    // Default mixture material of D and G
                    mat = comp::create<Material>(
                        skip_specular_mat ? "material::wavefrontobj_marginal_without_alpha" 
                                          : "material::wavefrontobj_mixture",
                        make_loc(m.name),
                        {
                            {"Kd", m.Kd},
                            {"mapKd", mapKd_loc},
                            {"Ks", m.Ks},
                            {"ax", std::max(1e-3_f, r / as)},
                            {"ay", std::max(1e-3_f, r * as)}
                        });
                    #endif
                }
                if (!mat) {
                    return false;
                }
                assets_map_[m.name] = int(assets_.size());
                assets_.push_back(std::move(mat));

                return true;
            });
        if (!result) {
            LM_THROW_EXCEPTION_DEFAULT(Error::IOError);
        }
    }
    
    virtual void create_primitives(const CreatePrimitiveFunc& createPrimitive) const override {
        for (auto [mesh, material, light] : groups_) {
            auto* meshp = assets_.at(mesh).get();
            auto* materialp = assets_.at(material).get();
            createPrimitive(
                meshp,
                materialp,
                light < 0 ? nullptr : assets_.at(light).get());
        }
    }

	virtual void foreach_node(const VisitNodeFuncType&) const override {
        LM_THROW_EXCEPTION_DEFAULT(Error::Unsupported);
	}
};

LM_COMP_REG_IMPL(Model_WavefrontObj, "model::wavefrontobj");

// ------------------------------------------------------------------------------------------------

class Mesh_WavefrontObj final  : public Mesh {
private:
    Model_WavefrontObj* model_;
    OBJMeshFace fs_;

public:
    LM_SERIALIZE_IMPL(ar) {
        ar(model_, fs_);
    }

    virtual void foreach_underlying(const ComponentVisitor& visit) override {
        comp::visit(visit, model_);
    }

public:
    virtual void construct(const Json& prop) override {
        model_ = prop["model_"].get<Model_WavefrontObj*>();
        fs_ = *prop["fs_"].get<const OBJMeshFace*>();
    }

    virtual void foreach_triangle(const ProcessTriangleFunc& processTriangle) const override {
        for (int fi = 0; fi < int(fs_.size())/3; fi++) {
            processTriangle(fi, triangle_at(fi));
        }
    }

    virtual Tri triangle_at(int face) const override {
        const auto& geo_ = model_->geo_;
        const auto f1 = fs_[3*face];
        const auto f2 = fs_[3*face+1];
        const auto f3 = fs_[3*face+2];
        return {
            { geo_.ps[f1.p], f1.n<0 ? Vec3() : geo_.ns[f1.n], f1.t<0 ? Vec2() : geo_.ts[f1.t] },
            { geo_.ps[f2.p], f2.n<0 ? Vec3() : geo_.ns[f2.n], f2.t<0 ? Vec2() : geo_.ts[f2.t] },
            { geo_.ps[f3.p], f3.n<0 ? Vec3() : geo_.ns[f3.n], f3.t<0 ? Vec2() : geo_.ts[f3.t] }
        };
    }

    virtual InterpolatedPoint surface_point(int face, Vec2 uv) const override {
        const auto& geo_ = model_->geo_;
        const auto i1 = fs_[3*face];
        const auto i2 = fs_[3*face+1];
        const auto i3 = fs_[3*face+2];
        const auto p1 = geo_.ps[i1.p];
        const auto p2 = geo_.ps[i2.p];
        const auto p3 = geo_.ps[i3.p];
        return {
            // Position
            math::mix_barycentric(p1, p2, p3, uv),
            // Normal. Use geometry normal if the attribute is missing.
            i1.n < 0 ? math::geometry_normal(p1, p2, p3)
                     : glm::normalize(math::mix_barycentric(
                         geo_.ns[i1.n], geo_.ns[i2.n], geo_.ns[i3.n], uv)),
            // Geometry normal
            math::geometry_normal(p1, p2, p3),
            // Texture coordinates
            i1.t < 0 ? Vec2(0) : math::mix_barycentric(
                geo_.ts[i1.t], geo_.ts[i2.t], geo_.ts[i3.t], uv)
        };
    }

    virtual int num_triangles() const override {
        return int(fs_.size()) / 3;
    }
};

LM_COMP_REG_IMPL(Mesh_WavefrontObj, "mesh::wavefrontobj");

// ------------------------------------------------------------------------------------------------

// Mixtured material without alpha texture
class Material_WavefrontObj_Mixture_Without_Alpha final : public Material {
private:
    Component::Ptr<Material> diffuse_;
    Component::Ptr<Material> glossy_;

    // Component indices
    enum {
        Comp_Diffuse = 0,   // Diffuse material
        Comp_Glossy = 1,   // Glossy material
    };

public:
    LM_SERIALIZE_IMPL(ar) {
        ar(diffuse_, glossy_);
    }

    virtual Component* underlying(const std::string& name) const override {
        if (name == "diffuse") return diffuse_.get();
        else if (name == "glossy") return glossy_.get();
        return nullptr;
    }

    virtual void foreach_underlying(const ComponentVisitor& visit) override {
        comp::visit(visit, diffuse_);
        comp::visit(visit, glossy_);
    }

private:
    // Get material by component index
    Material* material_by_comp(int comp) const {
        switch (comp) {
        case Comp_Diffuse:
            return diffuse_.get();
        case Comp_Glossy:
            return glossy_.get();
        }
        return nullptr;
    }

    // Compute selection weight
    Float diffuse_selection_weight(const PointGeometry& geom) const {
        const auto weight_d = [&]() {
            const auto weight_d = glm::compMax(*diffuse_->reflectance(geom));
            const auto weight_g = glm::compMax(*glossy_->reflectance(geom));
            if (weight_d == 0_f && weight_g == 0_f) {
                return 1_f;
            }
            return weight_d / (weight_d + weight_g);
        }();
        return weight_d;
    }

    // Component selection
    int sample_comp_select(Rng& rng, const PointGeometry& geom) const {
        // Difuse
        const auto weight_d = diffuse_selection_weight(geom);
        if (rng.u() < weight_d) {
            return Comp_Diffuse;
        }

        // Glossy
        return Comp_Glossy;
    }
    
    // Component selection PMF
    Float pdf_comp_select(const PointGeometry& geom, int comp) const {
        // Diffuse
        const auto weight_d = diffuse_selection_weight(geom);
        if (comp == Comp_Diffuse) {
            return weight_d;
        }

        // Glossy
        assert(comp == Comp_Glossy);
        return (1_f - weight_d);
    }

public:
    virtual void construct(const Json& prop) override {
        const auto Kd = json::value<Vec3>(prop, "Kd");
        const auto mapKd = json::value<std::string>(prop, "mapKd");
        const auto Ks = json::value<Vec3>(prop, "Ks");
        const auto ax = json::value<Float>(prop, "ax");
        const auto ay = json::value<Float>(prop, "ay");

        // Diffuse material
        diffuse_ = comp::create<Material>(
            "material::diffuse", make_loc("diffuse"), {
                {"Kd", Kd},
                {"mapKd", mapKd}
            });

        // Glossy material
        glossy_ = comp::create<Material>(
            "material::glossy", make_loc("glossy"), {
                {"Ks", Ks},
                {"ax", ax},
                {"ay", ay}
            });
    }

    virtual std::optional<MaterialDirectionSample> sample_direction(Rng& rng, const PointGeometry& geom, Vec3 wi, MaterialTransDir trans_dir) const override {
        const int comp = sample_comp_select(rng, geom);
        const auto* material = material_by_comp(comp);
        const auto s = material->sample_direction(rng, geom, wi, trans_dir);
        if (!s) {
            return {};
        }
        const auto f = eval(geom, wi, s->wo, trans_dir, {});
        const auto p = pdf_direction(geom, wi, s->wo, {});
        return MaterialDirectionSample{
            s->wo,
            f / p,
            false
        };
    }

    virtual std::optional<Vec3> reflectance(const PointGeometry& geom) const override {
        return diffuse_->reflectance(geom);
    }

    virtual Float pdf_direction(const PointGeometry& geom, Vec3 wi, Vec3 wo, bool) const override {
        // Evaluate p_sel(j) * p_j(wo)
        const auto eval_pdf = [&](int c) -> Float {
            // Consider only if wo can be samplable with the strategy c
            // All strategies are samplable each other.
            const auto p_sel = pdf_comp_select(geom, c);
            const auto p = [&]() -> Float {
                const auto* material = material_by_comp(c);
                return material->pdf_direction(geom, wi, wo, {});
            }();
            return p_sel * p;
        };

        // Compute marginal
        Float p_maginal = 0_f;
        p_maginal += eval_pdf(Comp_Diffuse);
        p_maginal += eval_pdf(Comp_Glossy);

        return p_maginal;
    }

    virtual Vec3 eval(const PointGeometry& geom, Vec3 wi, Vec3 wo, MaterialTransDir trans_dir, bool) const override {
        const auto eval_f = [&](int c) -> Vec3 {
            const auto f = [&]() -> Vec3 {
                const auto* material = material_by_comp(c);
                return material->eval(geom, wi, wo, trans_dir, {});
            }();
            return f;
        };

        Vec3 f_mixture(0_f);
        f_mixture += eval_f(Comp_Diffuse);
        f_mixture += eval_f(Comp_Glossy);

        return f_mixture;
    }
};

LM_COMP_REG_IMPL(Material_WavefrontObj_Mixture_Without_Alpha, "material::wavefrontobj_marginal_without_alpha");

// ------------------------------------------------------------------------------------------------

class Material_WavefrontObj_Mixture final : public Material {
private:
    Component::Ptr<Material> diffuse_;
    Component::Ptr<Material> glossy_;
    Component::Ptr<Material> alpha_mask_;
    Texture* mask_tex_ = nullptr;

    // Component indices
    enum {
        Comp_Diffuse = 0,   // Diffuse material
        Comp_Glossy  = 1,   // Glossy material
        Comp_Alpha   = 2,   // Alpha mask
    };

public:
    LM_SERIALIZE_IMPL(ar) {
        ar(diffuse_, glossy_, alpha_mask_, mask_tex_);
    }

    virtual Component* underlying(const std::string& name) const override {
        if (name == "diffuse") return diffuse_.get();
        else if (name == "glossy") return glossy_.get();
        else if (name == "alpha_mask") return alpha_mask_.get();
        return nullptr;
    }

    virtual void foreach_underlying(const ComponentVisitor& visit) override {
        comp::visit(visit, diffuse_);
        comp::visit(visit, glossy_);
        comp::visit(visit, alpha_mask_);
        comp::visit(visit, mask_tex_);
    }

private:
    // Get material by component index
    Material* material_by_comp(int comp) const {
        switch (comp) {
            case Comp_Diffuse:
                return diffuse_.get();
            case Comp_Glossy:
                return glossy_.get();
            case Comp_Alpha:
                return alpha_mask_.get();
        }
        return nullptr;
    }

    // Check if component is specular
    bool is_specular_comp(int comp) const {
        return comp == Comp_Alpha;
    }

    // Compute selection weight
    Float diffuse_selection_weight(const PointGeometry& geom) const {
        const auto weight_d = [&]() {
            const auto weight_d = glm::compMax(*diffuse_->reflectance(geom));
            const auto weight_g = glm::compMax(*glossy_->reflectance(geom));
            if (weight_d == 0_f && weight_g == 0_f) {
                return 1_f;
            }
            return weight_d / (weight_d + weight_g);
        }();
        return weight_d;
    }

    // Evaluate alpha value
    Float eval_alpha(const PointGeometry& geom) const {
        return !mask_tex_ ? 1_f : mask_tex_->eval_alpha(geom.t);
    }

    // Component selection
    int sample_comp_select(Rng& rng, const PointGeometry& geom) const {
        // Alpha mask
        const auto alpha = eval_alpha(geom);
        if (rng.u() > alpha) {
            return Comp_Alpha;
        }

        // Difuse
        const auto weight_d = diffuse_selection_weight(geom);
        if (rng.u() < weight_d) {
            return Comp_Diffuse;
        }

        // Glossy
        return Comp_Glossy;
    }
    
    // Component selection PMF
    Float pdf_comp_select(const PointGeometry& geom, int comp) const {
        // Alpha mask
        const auto alpha = eval_alpha(geom);
        if (comp == Comp_Alpha) {
            return 1_f - alpha;
        }

        // Diffuse
        const auto weight_d = diffuse_selection_weight(geom);
        if (comp == Comp_Diffuse) {
            return alpha * weight_d;
        }

        // Glossy
        assert(comp == Comp_Glossy);
        return alpha * (1_f - weight_d);
    }

    // Evaluate mixture weight
    Float eval_mix_weight(const PointGeometry& geom, int comp) const {
        const auto alpha = eval_alpha(geom);
        return comp == Comp_Alpha ? (1_f - alpha) : alpha;
    }

    // Check if the direction sampled with the strategy comp can be sampled with the other strategy.
    bool is_samplable(int comp, int other_comp) const {
        if (comp == Comp_Diffuse || comp == Comp_Glossy) {
            return other_comp != Comp_Alpha;
        }
        else if (comp == Comp_Alpha) {
            return other_comp == Comp_Alpha;
        }
        LM_UNREACHABLE_RETURN();
    }

public:
    virtual void construct(const Json& prop) override {
        const auto Kd = json::value<Vec3>(prop, "Kd");
        const auto mapKd = json::value<std::string>(prop, "mapKd");
        const auto Ks = json::value<Vec3>(prop, "Ks");
        const auto ax = json::value<Float>(prop, "ax");
        const auto ay = json::value<Float>(prop, "ay");

        // Diffuse material
        diffuse_ = comp::create<Material>(
            "material::diffuse", make_loc("diffuse"), {
                {"Kd", Kd},
                {"mapKd", mapKd}
            });

        // Glossy material
        glossy_ = comp::create<Material>(
            "material::glossy", make_loc("glossy"), {
                {"Ks", Ks},
                {"ax", ax},
                {"ay", ay}
            });

        // Alpha mask
        alpha_mask_ = comp::create<Material>(
            "material::mask", make_loc("alpha_mask"), {});
        if (!mapKd.empty()) {
            auto* texture = comp::get<Texture>(mapKd);
            if (texture && texture->has_alpha()) {
                mask_tex_ = texture;
            }
        }
    }

    virtual std::optional<MaterialDirectionSample> sample_direction(Rng& rng, const PointGeometry& geom, Vec3 wi, MaterialTransDir trans_dir) const override {
        const int comp = sample_comp_select(rng, geom);
        const auto* material = material_by_comp(comp);
        const auto s = material->sample_direction(rng, geom, wi, trans_dir);
        if (!s) {
            return {};
        }

#if 1
        // Skip the evaluation of delta component
        // because they are cancelled out.
        const auto f = eval(geom, wi, s->wo, trans_dir, false);
        const auto p = pdf_direction(geom, wi, s->wo, false);
        const auto C = f / p;
        return MaterialDirectionSample{
            s->wo,
            C,
            is_specular_comp(comp)
        };
#else
        // If the selected component is non-delta component,
        // compute f and p separately and compute the weight.
        if (!is_specular_comp(comp)) {
            const auto f = eval(geom, wi, s->wo);
            const auto p = pdf_direction(geom, wi, s->wo);
            const auto C = f / p;
            return MaterialDirectionSample{
                s->wo,
                C,
                false
            };
        }
        // Otherwise, compute f/p = weight for selecting comp
        // because delta components are cancelled out.
        else {
            const auto C = Vec3(eval_mix_weight(geom, comp));
            return MaterialDirectionSample{
                s->wo,
                C,
                true
            };
        }
#endif
    }

    virtual std::optional<Vec3> reflectance(const PointGeometry& geom) const override {
        return diffuse_->reflectance(geom);
    }

    virtual Float pdf_direction(const PointGeometry& geom, Vec3 wi, Vec3 wo, bool eval_delta) const override {
        // Evaluate p_sel(j) * p_j(wo)
        const auto eval_pdf = [&](int c) -> Float {
            // Consider only if wo can be samplable with the strategy c
            // All strategies are samplable each other.
            const auto p_sel = pdf_comp_select(geom, c);
            const auto p = [&]() -> Float {
                const auto* material = material_by_comp(c);
                return material->pdf_direction(geom, wi, wo, eval_delta);
            }();
            return p_sel * p;
        };

        if (geom.opposite(wi, wo)) {
            // If wi and wo lie in the opposite half-plane,
            // only Alpha strategy is samplable.
            return eval_pdf(Comp_Alpha);
        }
        else {
            // Compute marginal
            // Evaluate components except for specular components
            Float p_maginal = 0_f;
            p_maginal += eval_pdf(Comp_Diffuse);
            p_maginal += eval_pdf(Comp_Glossy);
            return p_maginal;
        }
    }

    virtual Vec3 eval(const PointGeometry& geom, Vec3 wi, Vec3 wo, MaterialTransDir trans_dir, bool eval_delta) const override {
        const auto eval_f = [&](int c) -> Vec3 {
            const auto w = eval_mix_weight(geom, c);
            const auto f = [&]() -> Vec3 {
                const auto* material = material_by_comp(c);
                return material->eval(geom, wi, wo, trans_dir, eval_delta);
            }();
            return w * f;
        };

        if (geom.opposite(wi, wo)) {
            return eval_f(Comp_Alpha);
        }
        else {
            Vec3 f_mixture(0_f);
            f_mixture += eval_f(Comp_Diffuse);
            f_mixture += eval_f(Comp_Glossy);
            return f_mixture;
        }
    }
};

LM_COMP_REG_IMPL(Material_WavefrontObj_Mixture, "material::wavefrontobj_mixture");

LM_NAMESPACE_END(LM_NAMESPACE)
