/*
    Lightmetrica - Copyright (c) 2019 Hisanari Otsu
    Distributed under MIT license. See LICENSE file for details.
*/

#include <pch.h>
#include <lm/core.h>
#include <lm/renderer.h>
#include <lm/scene.h>
#include <lm/film.h>
#include <lm/scheduler.h>
#include <lm/path.h>

LM_NAMESPACE_BEGIN(LM_NAMESPACE)

class Renderer_VolPT final : public Renderer {
private:
    Scene* scene_;
    Film* film_;
    int max_verts_;
    Float rr_prob_;
    std::optional<unsigned int> seed_;
    Component::Ptr<scheduler::Scheduler> sched_;

public:
    LM_SERIALIZE_IMPL(ar) {
        ar(scene_, film_, max_verts_, rr_prob_, sched_);
    }

    virtual void foreach_underlying(const ComponentVisitor& visit) override {
        comp::visit(visit, scene_);
        comp::visit(visit, film_);
        comp::visit(visit, sched_);
    }

public:
    virtual void construct(const Json& prop) override {
        scene_ = json::comp_ref<Scene>(prop, "scene");
        film_ = json::comp_ref<Film>(prop, "output");
        scene_->camera()->set_aspect_ratio(film_->aspect());
        max_verts_ = json::value<int>(prop, "max_verts");
        seed_ = json::value_or_none<unsigned int>(prop, "seed");
        rr_prob_ = json::value<Float>(prop, "rr_prob", .2_f);
        const auto sched_name = json::value<std::string>(prop, "scheduler");
        sched_ = comp::create<scheduler::Scheduler>(
            "scheduler::spi::" + sched_name, make_loc("scheduler"), prop);
    }

    virtual Json render() const override {
		scene_->require_renderable();

        film_->clear();
        const auto size = film_->size();
        const auto processed = sched_->run([&](long long, long long sample_index, int threadid) {
            LM_KEEP_UNUSED(sample_index);

            // Per-thread random number generator
            thread_local Rng rng(seed_ ? *seed_ + threadid : math::rng_seed());

            // Sample initial vertex
            const auto sE = path::sample_position(rng, scene_, TransDir::EL);
            const auto sE_comp = path::sample_component(rng, scene_, sE->sp);
            auto sp = sE->sp;
            int comp = sE_comp.comp;
            auto throughput = sE->weight * sE_comp.weight;

            // Perform random walk
            Vec3 wi{};
            Vec2 raster_pos{};
            for (int num_verts = 1; num_verts < max_verts_; num_verts++) {
                // Sample a NEE edge
                const auto samplable_by_nee = !path::is_specular_component(scene_, sp, comp);
                if (samplable_by_nee) [&] {
                    // Sample a light
                    const auto sL = path::sample_direct_light(rng, scene_, sp);
                    if (!sL) {
                        return;
                    }

                    // Recompute raster position for the primary edge
                    Vec2 rp = raster_pos;
                    if (num_verts == 1) {
                        const auto rp_ = path::raster_position(scene_, -sL->wo);
                        if (!rp_) { return; }
                        rp = *rp_;
                    }

                    // Transmittance
                    const auto Tr = path::eval_transmittance(rng, scene_, sp, sL->sp);
                    if (math::is_zero(Tr)) {
                        return;
                    }

                    // Evaluate BSDF
                    const auto wo = -sL->wo;
                    const auto fs = path::eval_contrb_direction(scene_, sp, wi, wo, comp, TransDir::EL, true);
                    if (math::is_zero(fs)) {
                        return;
                    }

                    // Evaluate and accumulate contribution
                    const auto C = throughput * Tr * fs * sL->weight;
                    film_->splat(rp, C);
                }();


                // Sample direction
                const auto s = path::sample_direction(rng, scene_, sp, wi, comp, TransDir::EL);
                if (!s) {
                    break;
                }

                // Compute and cache raster position
                if (num_verts == 1) {
                    raster_pos = *path::raster_position(scene_, s->wo);
                }

                // Sample next scene interaction
                const auto sd = path::sample_distance(rng, scene_, sp, s->wo);
                if (!sd) {
                    break;
                }

                // Update throughput
                throughput *= s->weight * sd->weight;


                // Accumulate contribution from emissive interaction
                if (!samplable_by_nee && scene_->is_light(sd->sp)) {
                    const auto spL = sd->sp.as_type(SceneInteraction::LightEndpoint);
                    const auto woL = -s->wo;
                    const auto Le = path::eval_contrb_direction(scene_, spL, {}, woL, comp, TransDir::LE, true);
                    const auto C = throughput * Le;
                    film_->splat(raster_pos, C);
                }
                
                // Termination on a hit with environment
                if (sd->sp.geom.infinite) {
                    break;
                }

                // Russian roulette
                if (num_verts > 5) {
                    const auto q = glm::max(rr_prob_, 1_f - glm::compMax(throughput));
                    if (rng.u() < q) {
                        break;
                    }
                    throughput /= 1_f - q;
                }

                // Sample component
                const auto s_comp = path::sample_component(rng, scene_, sd->sp);
                throughput *= s_comp.weight;

                // Update information
                wi = -s->wo;
                sp = sd->sp;
                comp = s_comp.comp;
            }
        });

        // Rescale film
        film_->rescale(Float(size.w* size.h) / processed);

        return { {"processed", processed} };
    }
};

LM_COMP_REG_IMPL(Renderer_VolPT, "renderer::volpt");

LM_NAMESPACE_END(LM_NAMESPACE)
