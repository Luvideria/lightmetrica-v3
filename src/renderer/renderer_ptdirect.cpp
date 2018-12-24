/*
    lightmetrica - copyright (c) 2018 hisanari otsu
    distributed under mit license. see license file for details.
*/

#include <pch.h>
#include <lm/user.h>
#include <lm/renderer.h>
#include <lm/scene.h>
#include <lm/film.h>
#include <lm/parallel.h>

LM_NAMESPACE_BEGIN(LM_NAMESPACE)

class Renderer_PTDirect final : public Renderer {
private:
    Film* film_;
    long long spp_;
    int maxLength_;
    int rngSeed_ = 42;

public:
    virtual bool construct(const Json& prop) override {
        film_ = comp::cast<lm::Film>(lm::getAsset(prop["output"].get<std::string>()));
        if (!film_) {
            return false;
        }
        spp_ = prop["spp"];
        maxLength_ = prop["maxLength"];
        return true;
    }

    virtual void render(const Scene* scene) const override {
        const auto [w, h] = film_->size();
        parallel::foreach(w*h, [&](long long index, int threadId) -> void {
            // Per-thread random number generator
            thread_local Rng rng(rngSeed_ + threadId);

            // Pixel positions
            const int x = int(index % w);
            const int y = int(index / w);

            // Estimate pixel contribution
            Vec3 L(0_f);
            for (long long i = 0; i < spp_; i++) {
                // Path throughput
                Vec3 throughput(1_f);

				// Incident direction and current surface point
				Vec3 wi;
				SurfacePoint sp;

                // Initial sampleRay function
                std::function<std::optional<RaySample>()> sampleRay = [&]() {
                    Float dx = 1_f/w, dy = 1_f/h;
                    return scene->samplePrimaryRay(rng, {dx*x, dy*y, dx, dy});
                };

                // Perform random walk
                for (int length = 0; length < maxLength_; length++) {
                    // Sample a ray
                    const auto s = sampleRay();
                    if (!s || math::isZero(s->weight)) {
                        break;
                    }

					// Sample a NEE edge
					const bool enableNEE = length > 0 && !scene->isSpecular(s->sp);
					if (enableNEE) [&] {
						// Sample a light
						const auto sL = scene->sampleLight(rng, s->sp);
						if (!sL) {
							return;
						}
						if (scene->intersect(Ray{s->sp.p, sL->wo}, Eps, sL->d*(1_f-Eps))) {
							return;
						}
						// Evaluate and accumulate contribution
						L += throughput * scene->evalBsdf(s->sp, wi, sL->wo) * sL->weight;
					}();

                    // Intersection to next surface
                    const auto hit = scene->intersect(s->ray());
                    if (!hit) {
                        break;
                    }

					// Update throughput
					throughput *= s->weight;

                    // Accumulate contribution from light
                    //if (scene->isLight(*hit)) {
                    //    L += throughput * scene->evalContrbEndpoint(*hit, -s->wo);
                    //}

                    // Russian roulette
                    if (length > 3) {
                        const auto q = glm::max(.2_f, 1_f - glm::compMax(throughput));
                        if (rng.u() < q) {
                            break;
                        }
                        throughput /= 1_f - q;
                    }

                    // Update
					wi = -s->wo;
					sp = *hit;
                    sampleRay = [&]() {
                        return scene->sampleRay(rng, sp, wi);
                    };
                }
            }
            L /= spp_;

            // Set color of the pixel
            film_->setPixel(x, y, L);
        });
    }
};

LM_COMP_REG_IMPL(Renderer_PTDirect, "renderer::ptdirect");

LM_NAMESPACE_END(LM_NAMESPACE)
