/*
    Lightmetrica - Copyright (c) 2019 Hisanari Otsu
    Distributed under MIT license. See LICENSE file for details.
*/

#pragma once

#include "component.h"
#include "math.h"
#include "surface.h"
#include "scenenode.h"

LM_NAMESPACE_BEGIN(LM_NAMESPACE)

/*!
    \addtogroup scene
    @{
*/

/*!
    \brief Result of ray sampling.

    \rst
    This structure represents the result of ray sampling
    used by the functions of :cpp:class:`lm::Scene` class.
    \endrst
*/
struct RaySample {
    SceneInteraction sp;   //!< Sampled scene interaction.
    int comp;              //!< Sampled component index.
    Vec3 wo;               //!< Sampled direction.
    Vec3 weight;           //!< Contribution divided by probability.

    /*!
        \brief Get a ray from the sample.
        
        \rst
        This function constructs a :cpp:class:`lm::Ray`
        structure from the ray sample.
        \endrst
    */
    Ray ray() const {
        assert(!sp.geom.infinite);
        return { sp.geom.p, wo };
    }
};

/*!
*/
struct DirectionSample {
    Vec3 wo;
    int comp;
    Vec3 weight;
};

/*!
    \brief Result of distance sampling.
*/
struct DistanceSample {
    SceneInteraction sp;    //!< Sampled interaction point.
    Vec3 weight;            //!< Contribution divided by probability.
};

// ------------------------------------------------------------------------------------------------

/*!
    \brief Scene.

    \rst
    This class represent a component interface for a scene.
    A scene is responsible for sampling of a ray emitted from a point inside a scene,
    evaluation of directional terms given a point in the scene,
    ray-scene intersection, visibility query, etc.
    The class is a basic building block to construct your own renderer.
    
    A scene is also responsible for managing the collection of assets (e.g., meshes, materials, etc.).
    Underlying assets are accessed via standard query functions like
    :cpp:func:`lm::Component::underlying`. The class provides the feature for internal usage
    and the user may not want to use this interface directly.
    The feature of the class is exposed by ``user`` namespace.
    \endrst
*/
class Scene : public Component {
public:
    /*!
        \brief Reset the scene.
    */
    virtual void reset() = 0;

    // --------------------------------------------------------------------------------------------

    //
    // Scene graph manipulation and access
    //

    /*!
        \brief Get index of the root node.
        \return Node index.
    */
    virtual int root_node() = 0;

	/*!
		\brief Create primitive node.
        \param prop Property containing references to the scene components.
        \return Index of the created node.

		\rst
		This function create a primitive scene node and add it to the scene.
		The references to the assets are specified in ``prop``.
		The accepted assets types are ``mesh``, ``material``, ``light``, ``camera``, and ``medium``.
		This function returns node index if succedded.
		\endrst
	*/
	virtual int create_primitive_node(const Json& prop) = 0;

	/*!
		\brief Create group node.
		\param transform Local transoform of the node.

		\rst
		This function creates a group scen node and add it to the scene.
		''transform'' specifies the transformation of the node to be applied to the child nodes.
		This function returns node index if succedded.
		\endrst
	*/
	virtual int create_group_node(Mat4 transform) = 0;

	/*!
		\brief Create instance group node.

		\rst
		This function creates a special group node for instance group.
		The child nodes of this node are considered as an instance group.
		This function returns node index if succedded.
		\endrst
	*/
	virtual int create_instance_group_node() = 0;

    /*!
        \brief Add primitive group.
        \param parent Parent node index.
        \param child Child node index being added.

        \rst
        This function adds a primitive group to the scene
        and returns index of the group.
        If the group with the same name is already created,
        this function returns the index of the registered group.
        \endrst
    */
    virtual void add_child(int parent, int child) = 0;

    /*!
        \brief Add child node from model asset.
        \param parent Index of the parent node.
        \param model_loc Locator of the model asset.
    */
    virtual void add_child_from_model(int parent, const std::string& model_loc) = 0;

    /*!
        \brief Create group node from model asset.
        \param model_loc Locator of the model asset.
        \return Index of the created node.
    */
    virtual int create_group_from_model(const std::string& model_loc) = 0;

    /*!
        \brief Create primitive(s) and add to the scene.
        \param prop Properties for configuration.
        \see `example/quad.cpp`
        \see `example/raycast.cpp`

        \rst
        This function creates primitive(s) and registers to the framework.
        A primitive is a scene object associating the assets such as
        meshes or materials. The coordinates of the object is
        speficied by a 4x4 transformation matrix.
        We can use the same assets to define different primitives
        with different transformations.

        If ``model`` parameter is specified,
        the function will register primitives generated from the model.
        In this case, the transformation is applied to all primitives to be generated.
        \endrst
    */
    void add_primitive(const Json& prop) {
        add_transformed_primitive(Mat4(1_f), prop);
    }

    /*!
        \brief Create primitive(s) and add to the scene with transform.
        \param transform Transformation matrix.
        \param prop Properties for configuration.
    */
    void add_transformed_primitive(Mat4 transform, const Json& prop) {
        auto t = create_group_node(transform);
        if (prop.find("model") != prop.end()) {
            add_child_from_model(t, prop["model"]);
        }
        else {
            add_child(t, create_primitive_node(prop));
        }
        add_child(root_node(), t);
    }

    /*!
        \brief Callback function to traverse the scene nodes.
        \param node Current node.
        \param global_transform Global transform applied to the node.
    */
    using NodeTraverseFunc = std::function<void(const SceneNode& node, Mat4 global_transform)>;

    /*!
        \brief Iterate primitive nodes in the scene.
        \param traverse_func Function being called for each traversed primitive node.
        
        \rst
        This function traverses the primitive nodes in the scene graph.
        For each primitive node, global transformation is computed and passed as
        an argument of the callback function.
        Note that this function does not traverse intermediate group nodes.
        If you want to traverse also the group node, consider to use :cpp:func:`Scene::visit_node`.
        \endrst
    */
    virtual void traverse_primitive_nodes(const NodeTraverseFunc& traverse_func) const = 0;

    /*!
        \brief Callback function to traverse the scene nodes.
        \param node Scene node.
    */
    using VisitNodeFunc = std::function<void(const SceneNode& node)>;

    /*!
        \brief Traverse a node in the scene.
        \param node_index Note index where the traverse starts.
        \param visit Callback function being called for each traversed node.

        \rst
        This function traverse a node in the scene graph.
        Unlike :cpp:func:`Scene::traverse_primitive_nodes`, this function
        can be used to traverse all kinds of scene nodes in the scene graph.
        Be careful the user is responsible to call this function to traverse the node recursively.
        \endrst
    */
    virtual void visit_node(int node_index, const VisitNodeFunc& visit) const = 0;

    /*!
        \brief Get scene node by index.
        \param node_index Scene node index.
        \return Scene node.
    */
    virtual const SceneNode& node_at(int node_index) const = 0;

	/*!
		\brief Get number of nodes.

		\rst
		Note that the scene contains at least one node (root node).
		\endrst
	*/
	virtual int num_nodes() const = 0;

	/*!
		\brief Get number of lights in the scene.
	*/
	virtual int num_lights() const = 0;

	/*!
		\brief Get camera node index.

		\rst
		This function returns -1 if there is camera in the scene.
		\endrst
	*/
	virtual int camera_node() const = 0;

	/*!
		\brief Get environment map node index.

		\rst
		This function returns -1 if there is no environment light in the scene.
		\endrst
	*/
	virtual int env_light_node() const = 0;

	// --------------------------------------------------------------------------------------------

    //
    // Scene requirement checking
    //

	/*!
		\brief Throws an exception if there is no primitive in the scene.
	*/
	void require_primitive() const {
		if (num_nodes() > 1) {
			return;
		}
		LM_THROW_EXCEPTION(Error::Unsupported,
			"Missing primitives. Use lm::primitive() function to add primitives.");
	}

	/*!
		\brief Throws an exception if there is no camera in the scene.
	*/
	void require_camera() const {
		if (camera_node() != -1) {
			return;
		}
		LM_THROW_EXCEPTION(Error::Unsupported,
			"Missing camera primitive. Use lm::primitive() function to add camera primitive.");
	}

	/*!
		\brief Throws an exception if there is no light in the scene.
	*/
	void require_light() const {
		if (num_lights() > 0) {
			return;
		}
		LM_THROW_EXCEPTION(Error::Unsupported,
			"No light in the scene. Add at least one light sources to the scene.");
	}

	/*!
		\brief Throws an exception if there is no accel created for the scene.
	*/
	void require_accel() const {
		if (accel()) {
			return;
		}
		LM_THROW_EXCEPTION(Error::Unsupported,
			"Missing acceleration structure. Use lm::build() function before rendering.");
	}

	/*!
		\brief Throws an exception if there the scene is not renderable.

		\rst
		This function is equivalent to calling the following functions:

		- :cpp:func:`require_primitive`
		- :cpp:func:`require_camera`
		- :cpp:func:`require_light`
		- :cpp:func:`require_accel`
		\endrst
	*/
	void require_renderable() const {
		require_primitive();
		require_camera();
		require_light();
		require_accel();
	}

    // --------------------------------------------------------------------------------------------

    //
    // Ray-scene intersection
    //

    /*!
        \brief Get underlying acceleration structure.
        \return Instance.
    */
    virtual Accel* accel() const = 0;

    /*!
        \brief Set underlying acceleration structure.
        \param accel_loc Locator to the accel asset.
    */
    virtual void set_accel(const std::string& accel_loc) = 0;

    /*!
        \brief Build acceleration structure.
    */
    virtual void build() = 0;

    /*!
        \brief Compute closest intersection point.
        \param ray Ray.
        \param tmin Lower bound of the valid range of the ray.
        \param tmax Upper bound of the valid range of the ray.
        
        \rst
        This function computes closest intersection point between the given ray and the scene
        utilizing underlying acceleration structure of the scene.
        If no intersection happens, this function returns ``nullopt``.
        Note that if the scene contains environment light, this function returns scene intersection structure
        indicating the intersection with infinite point.
        This can be examined by checking :cpp:member:`PointGeometry::infinite` being ``true``.
        \endrst
    */
    virtual std::optional<SceneInteraction> intersect(Ray ray, Float tmin = Eps, Float tmax = Inf) const = 0;

    /*!
        \brief Check if two surface points are mutually visible.
        \param sp1 Scene interaction of the first point.
        \param sp2 Scene interaction of the second point.
        \return Returns true if two points are mutually visible, otherwise returns false.
    */
    bool visible(const SceneInteraction& sp1, const SceneInteraction& sp2) const {
        const auto visible_ = [this](const SceneInteraction& sp1, const SceneInteraction& sp2) -> bool {
            assert(!sp1.geom.infinite);
            const auto wo = sp2.geom.infinite
                ? -sp2.geom.wo
                : glm::normalize(sp2.geom.p - sp1.geom.p);
            const auto tmax = sp2.geom.infinite
                ? Inf - 1_f
                : [&]() {
                    const auto d = glm::distance(sp1.geom.p, sp2.geom.p);
                    return d * (1_f - Eps);
                }();
            // Exclude environent light from intersection test with tmax < Inf
            return !intersect(Ray{sp1.geom.p, wo}, Eps, tmax);
        };
        if (sp1.geom.infinite) {
            return visible_(sp2, sp1);
        }
        else {
            return visible_(sp1, sp2);
        }
    }

    // --------------------------------------------------------------------------------------------

    //
    // Primitive type checking
    //

    /*!
        \brief Check if given surface point is light.
        \param sp Scene interaction.
        \return True if scene interaction is light.
    */
    virtual bool is_light(const SceneInteraction& sp) const = 0;

    /*!
        \brief Check if given surface point is specular.
        \param sp Scene intersection.
		\param comp Component index.
        \return True if scene interaction is specular.

        \rst
        Scene interaction is specular if the material, light, or camera associated
        with point specified by scene intersection contains delta function.
        \endrst
    */
    virtual bool is_specular(const SceneInteraction& sp, int comp) const = 0;

    // --------------------------------------------------------------------------------------------

    //
    // Ray sampling
    //
    // Samples the joint distribution of the scene interaction x and the direction wo
    // originated from x. (x, wo) ~ p(x, wo).
    //

    /*!
        \brief Generate a primary ray.
        \param rp Raster position in [0,1]^2.
        \param aspect Aspect ratio of the film.
        \return Generated primary ray.

        \rst
        This function deterministically generates a primary ray
        corresponding to the given raster position.
        \endrst
    */
    virtual Ray primary_ray(Vec2 rp, Float aspect) const = 0;

    /*!
        \brief Sample a ray given surface point and incident direction.
        \param rng Random number generator.
        \param sp Surface interaction.
        \param wi 

        \rst
        This function samples a ray given the scene interaction.
        According to the types of scene interaction, this function samples a different
        types of the ray from the several distributions.

        (1) If the scene interaction is ``terminator``, this function samples a primary ray
            according type types of the terminator (camera or light). ``wi`` is ignored in this case.

        (2) If the scene interaction is not ``terminator``, this function samples
            a ray from the associated distribution to BSDF or phase function
            given the interaction event ``sp`` and incident ray direction ``wi``.

        In both cases, this function returns ``nullopt`` if the sampling failed,
        or the case when the early return is possible for instance when 
        the evaluated contribution of the sampled direction is zero.
        \endrst
    */
    virtual std::optional<RaySample> sample_ray(Rng& rng, const SceneInteraction& sp, Vec3 wi) const = 0;

    // --------------------------------------------------------------------------------------------

    //
    // Direction sampling
    //
    // wo ~ p_{\sigma^\bot}(wo).
    //

    /*!
    */
    virtual std::optional<DirectionSample> sample_direction(Rng& rng, const SceneInteraction& sp, Vec3 wi) const = 0;

    /*!
        \brief Evaluate pdf for direction sampling.
        \param sp Scene interaction.
		\param comp Component index.
        \param wi Incident ray direction.
        \param wo Sampled outgoing ray direction.
        \return Evaluated pdf.

        \rst
        This function evaluates pdf according to *projected solid angme measure* if ``sp.geom.degenerated=false``
        and *solid angme measure* if ``sp.geom.degenerated=true``
        utlizing corresponding densities from which the direction is sampled.
        \endrst
    */
    virtual Float pdf_direction(const SceneInteraction& sp, int comp, Vec3 wi, Vec3 wo) const = 0;

    // --------------------------------------------------------------------------------------------

    //
    // Direct endpoint sampling
    //
    // Samples a direction to a light or a sensor given a current x.
    // wo ~ p_{\sigma^\bot}(wo | x)
    //

    /*!
        \brief Sample direction to a light given a scene interaction.
        \param rng Random number generator.
        \param sp Scene interaction.

        \rst
        This function samples a ray to the light given a scene interaction.
        Be careful not to confuse the sampled ray with the ray sampled via :cpp:func:`Scene::sample_ray`
        function from a light source. Both rays are sampled from the different distributions
        and if you want to evaluate densities you want to use different functions.
        \endrst
    */
    virtual std::optional<RaySample> sample_direct_light(Rng& rng, const SceneInteraction& sp) const = 0;

    /*!
    */
    virtual std::optional<RaySample> sample_direct_camera(Rng& rng, const SceneInteraction& sp, Float aspect) const = 0;

    /*!
        \brief Evaluate pdf for endpoint sampling given a scene interaction.
        \param sp Scene interaction.
        \param sp_endpoint Sampled scene interaction of the endpoint.
		\param comp_endpoint Component index of the endpoint.
        \param wo Sampled outgoing ray directiom *from* the endpoint.

        \rst
        This function evaluate pdf for the ray sampled via :cpp:func:`Scene::sample_direct_light`
        or :cpp:func:`Scene::sample_direct_camera`.
        Be careful ``wo`` is the outgoing direction originated from ``sp_endpoint``, not ``sp``.
        \endrst
    */
    virtual Float pdf_direct(const SceneInteraction& sp, const SceneInteraction& sp_endpoint, int comp_endpoint, Vec3 wo) const = 0;

    // --------------------------------------------------------------------------------------------

    //
    // Distance sampling
    //

    /*!
        \brief Sample a distance in a ray direction.
        \param rng Random number generator.
        \param sp Scene interaction.
        \param wo Ray direction.

        \rst
        This function samples either a point in a medium or a point on the surface.
        Note that we don't provide corresponding pdf function because 
        some underlying distance sampling technique might not have the analitical representation.
        \endrst
    */
    virtual std::optional<DistanceSample> sample_distance(Rng& rng, const SceneInteraction& sp, Vec3 wo) const = 0;

    /*!
        \brief Evaluate transmittance.
        \param rng Random number generator.
        \param sp1 Scene interaction of the first point.
        \param sp2 Scene interaction of the second point.
        
        \rst
        This function evaluates transmittance between two scene interaction events.
        This function might need a random number generator
        because heterogeneous media needs stochastic estimation.
        If the space between ``sp1`` and ``sp2`` is vacuum (i.e., no media),
        this function is conceptually equivalent to :cpp:func:`Scene::visible`.
        \endrst
    */
    virtual Vec3 eval_transmittance(Rng& rng, const SceneInteraction& sp1, const SceneInteraction& sp2) const = 0;

    // --------------------------------------------------------------------------------------------

    //
    // Evaluating contribution
    //

    /*!
        \brief Compute a raster position.
        \param wo Primary ray direction.
        \param aspect Aspect ratio of the film.
        \return Raster position.
    */
    virtual std::optional<Vec2> raster_position(Vec3 wo, Float aspect) const = 0;

    /*!
        \brief Evaluate directional contribution.
        \param sp Scene interaction.
		\param comp Component index.
        \param wi Incident ray direction.
        \param wo Outgoing ray direction.
        \return Evaluated contribution.

        \rst
        This function evaluate directional contribution according to the scene interaction type.
        
        (1) If the scene interaction is endpoint and on a light,
            this function evaluates luminance function.

        (2) If the scene interaction is endpoint and on a sensor,
            this function evaluates importance function.
        
        (3) If the scene interaction is not endpoint and on a surface,
            this function evaluates BSDF.

        (4) If the scene interaction is in a medium,
            this function evaluate phase function.

        Note that the scene interaction obtained from :cpp:func:`Scene::intersect` or 
        :cpp:func:`Scene::sample_distance` is not an endpont
        even if it might represent either a light or a sensor.
        In this case, you want to use :cpp:func:`Scene::eval_contrb_endpoint`
        to enforce an evaluation as an endpoint.
        \endrst
    */
    virtual Vec3 eval_contrb(const SceneInteraction& sp, int comp, Vec3 wi, Vec3 wo) const = 0;

    /*!
        \brief Evaluate positional contribution of the endpoint.
    */
    virtual Vec3 eval_contrb_endpoint(const SceneInteraction& sp) const = 0;

#if 0
    /*!
        \brief Evaluate endpoint contribution.
		\param sp Surface interaction.
		\param wo Outgoing ray direction.

        \rst
        This function evaluates

        (1) If the scene interaction *contains* a light component,
            this function evaluates luminance function.

        (2) If the scene interaction *contains* a sensor component,
            this function evaluates importance function.

        That is, this function enforces the evaluation as an endpoint
        irrespective to the value of ``sp.endpoint``.
        \endrst
    */
    virtual Vec3 eval_contrb_endpoint(const SceneInteraction& sp, Vec3 wo) const = 0;
#endif

    /*!
        \brief Evaluate reflectance (if available).
        \param sp Surface interaction.
		\param comp Component index.

        \rst
        This function evaluate reflectance if ``sp`` is on a surface
        and the associated material implements :cpp:func:`Material::reflectance` function.
        \endrst
    */
    virtual std::optional<Vec3> reflectance(const SceneInteraction& sp, int comp) const = 0;
};

/*!
    @}
*/

LM_NAMESPACE_END(LM_NAMESPACE)
