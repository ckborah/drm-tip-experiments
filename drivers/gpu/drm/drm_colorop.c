/*
 * Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <drm/drm_colorop.h>
#include <drm/drm_print.h>
#include <drm/drm_drv.h>
#include <drm/drm_plane.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * A colorop represents a single color operation. Colorops are chained
 * via the NEXT property and make up color pipelines. Color pipelines
 * are advertised and selected via the COLOR_PIPELINE &drm_plane
 * property.
 *
 * A colorop will be of a certain type, advertised by the read-only TYPE
 * property. Each type of colorop will advertise a different set of
 * properties and is programmed in a different manner. Types can be
 * enumerated 1D curves, 1D LUTs, 3D LUTs, matrices, etc. See the
 * &drm_colorop_type documentation for information on each type.
 *
 * If a colorop advertises the BYPASS property it can be bypassed.
 *
 * Since colorops cannot stand-alone and are used to describe colorop
 * operations on a plane they don't have their own locking mechanism but
 * are locked and programmed along with their associated &drm_plane.
 *
 * Colorops are only advertised and valid for atomic drivers and atomic
 * userspace that signals the DRM_CLIENT_CAP_PLANE_COLOR_PIPELINE client
 * cap. When a driver advertises the COLOR_PIPELINE property on a
 * &drm_plane and userspace signals the
 * DRM_CLIENT_CAP_PLANE_COLOR_PIPELINE the driver shall ignore all other
 * plane color properties, such as COLOR_ENCODING and COLOR_RANGE.
 *
 * More information about colorops and color pipelines can be found at
 * rfc/color_pipeline.rst.
 */

static const struct drm_prop_enum_list drm_colorop_type_enum_list[] = {
	{ DRM_COLOROP_1D_CURVE, "1D Curve" },
	{ DRM_COLOROP_1D_LUT, "1D Curve Custom LUT" },
	{ DRM_COLOROP_CTM_3X4, "3x4 Matrix"},
	{ DRM_COLOROP_MULTIPLIER, "Multiplier"},
};

static const char * const colorop_curve_1d_type_names[] = {
	[DRM_COLOROP_1D_CURVE_SRGB_EOTF] = "sRGB EOTF",
	[DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF] = "sRGB Inverse EOTF",
	[DRM_COLOROP_1D_CURVE_BT2020_INV_OETF] = "BT.2020 Inverse OETF",
	[DRM_COLOROP_1D_CURVE_BT2020_OETF] = "BT.2020 OETF",
	[DRM_COLOROP_1D_CURVE_PQ_125_EOTF] = "PQ 125 EOTF",
	[DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF] = "PQ 125 Inverse EOTF",
};

static const struct drm_prop_enum_list drm_colorop_lut1d_interpolation_list[] = {
	{ DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR, "Linear" },
};

/* Init Helpers */

static int drm_colorop_init(struct drm_device *dev, struct drm_colorop *colorop,
			    struct drm_plane *plane, enum drm_colorop_type type,
			    bool allow_bypass)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_property *prop;
	int ret = 0;

	ret = drm_mode_object_add(dev, &colorop->base, DRM_MODE_OBJECT_COLOROP);
	if (ret)
		return ret;

	colorop->base.properties = &colorop->properties;
	colorop->dev = dev;
	colorop->type = type;
	colorop->plane = plane;
	colorop->next = NULL;

	list_add_tail(&colorop->head, &config->colorop_list);
	colorop->index = config->num_colorop++;

	/* add properties */

	/* type */
	prop = drm_property_create_enum(dev,
					DRM_MODE_PROP_IMMUTABLE,
					"TYPE", drm_colorop_type_enum_list,
					ARRAY_SIZE(drm_colorop_type_enum_list));

	if (!prop)
		return -ENOMEM;

	colorop->type_property = prop;

	drm_object_attach_property(&colorop->base,
				   colorop->type_property,
				   colorop->type);

	if (allow_bypass) {
		/* bypass */
		prop = drm_property_create_bool(dev, DRM_MODE_PROP_ATOMIC,
						"BYPASS");
		if (!prop)
			return -ENOMEM;

		colorop->bypass_property = prop;
		drm_object_attach_property(&colorop->base,
					colorop->bypass_property,
					1);
	}

	/* next */
	prop = drm_property_create_object(dev, DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_ATOMIC,
					  "NEXT", DRM_MODE_OBJECT_COLOROP);
	if (!prop)
		return -ENOMEM;
	colorop->next_property = prop;
	drm_object_attach_property(&colorop->base,
				   colorop->next_property,
				   0);

	return ret;
}

/**
 * drm_colorop_curve_1d_init - Initialize a DRM_COLOROP_1D_CURVE
 *
 * @dev: DRM device
 * @colorop: The drm_colorop object to initialize
 * @plane: The associated drm_plane
 * @supported_tfs: A bitfield of supported drm_colorop_curve_1d_init enum values,
 *                 created using BIT(curve_type) and combined with the OR '|'
 *                 operator.
 * @allow_bypass: true if BYPASS property should be created, false if bypass of
 *                this colorop is not possible
 * @return zero on success, -E value on failure
 */
int drm_colorop_curve_1d_init(struct drm_device *dev, struct drm_colorop *colorop,
			      struct drm_plane *plane, u64 supported_tfs,
			      bool allow_bypass)
{
	struct drm_prop_enum_list enum_list[DRM_COLOROP_1D_CURVE_COUNT];
	int i, len;

	struct drm_property *prop;
	int ret;

	if (!supported_tfs) {
		drm_err(dev,
			"No supported TFs for new 1D curve colorop on [PLANE:%d:%s]\n",
			plane->base.id, plane->name);
		return -EINVAL;
	}

	if ((supported_tfs & -BIT(DRM_COLOROP_1D_CURVE_COUNT)) != 0) {
		drm_err(dev, "Unknown TF provided on [PLANE:%d:%s]\n",
			plane->base.id, plane->name);
		return -EINVAL;
	}

	ret = drm_colorop_init(dev, colorop, plane, DRM_COLOROP_1D_CURVE,
			       allow_bypass);
	if (ret)
		return ret;

	len = 0;
	for (i = 0; i < DRM_COLOROP_1D_CURVE_COUNT; i++) {
		if ((supported_tfs & BIT(i)) == 0)
			continue;

		enum_list[len].type = i;
		enum_list[len].name = colorop_curve_1d_type_names[i];
		len++;
	}

	if (WARN_ON(len <= 0))
		return -EINVAL;


	/* initialize 1D curve only attribute */
	prop = drm_property_create_enum(dev, DRM_MODE_PROP_ATOMIC, "CURVE_1D_TYPE",
					enum_list, len);

	if (!prop)
		return -ENOMEM;

	colorop->curve_1d_type_property = prop;
	drm_object_attach_property(&colorop->base, colorop->curve_1d_type_property,
				   enum_list[0].type);
	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_colorop_curve_1d_init);

static int drm_colorop_create_data_prop(struct drm_device *dev, struct drm_colorop *colorop)
{
	struct drm_property *prop;

	/* data */
	prop = drm_property_create(dev, DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
					"DATA", 0);
	if (!prop)
		return -ENOMEM;

	colorop->data_property = prop;
	drm_object_attach_property(&colorop->base,
				   colorop->data_property,
				   0);

	return 0;
}

/**
 * drm_colorop_curve_1d_lut_init - Initialize a DRM_COLOROP_1D_LUT
 *
 * @dev: DRM device
 * @colorop: The drm_colorop object to initialize
 * @plane: The associated drm_plane
 * @lut_size: LUT size supported by driver
 * @lut1d_interpolation: 1D LUT interpolation type
 * @allow_bypass: true if BYPASS property should be created, false if bypass of
 *                this colorop is not possible
 * @return zero on success, -E value on failure
 */
int drm_colorop_curve_1d_lut_init(struct drm_device *dev, struct drm_colorop *colorop,
				  struct drm_plane *plane, uint32_t lut_size,
				  enum drm_colorop_lut1d_interpolation_type lut1d_interpolation,
				  bool allow_bypass)
{
	struct drm_property *prop;
	int ret;

	ret = drm_colorop_init(dev, colorop, plane, DRM_COLOROP_1D_LUT,
			       allow_bypass);
	if (ret)
		return ret;

	/* initialize 1D LUT only attribute */
	/* LUT size */
	prop = drm_property_create_range(dev, DRM_MODE_PROP_IMMUTABLE, "SIZE",
					 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;

	colorop->size_property = prop;
	drm_object_attach_property(&colorop->base, colorop->size_property, lut_size);

	/* Interpolation */
	prop = drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE, "LUT1D_INTERPOLATION",
					drm_colorop_lut1d_interpolation_list,
					ARRAY_SIZE(drm_colorop_lut1d_interpolation_list));
	if (!prop)
		return -ENOMEM;

	colorop->lut1d_interpolation_property = prop;
	drm_object_attach_property(&colorop->base, prop, lut1d_interpolation);
	colorop->lut1d_interpolation = lut1d_interpolation;

	/* data */
	ret = drm_colorop_create_data_prop(dev, colorop);
	if (ret)
		return ret;

	drm_colorop_reset(colorop);
	colorop->state->size = lut_size;

	return 0;
}
EXPORT_SYMBOL(drm_colorop_curve_1d_lut_init);

int drm_colorop_ctm_3x4_init(struct drm_device *dev, struct drm_colorop *colorop,
			     struct drm_plane *plane, bool allow_bypass)
{
	int ret;

	ret = drm_colorop_init(dev, colorop, plane, DRM_COLOROP_CTM_3X4,
			       allow_bypass);
	if (ret)
		return ret;

	ret = drm_colorop_create_data_prop(dev, colorop);
	if (ret)
		return ret;

	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_colorop_ctm_3x4_init);

/**
 * drm_colorop_mult_init - Initialize a DRM_COLOROP_MULTIPLIER
 *
 * @dev: DRM device
 * @colorop: The drm_colorop object to initialize
 * @plane: The associated drm_plane
 * @allow_bypass: true if BYPASS property should be created, false if bypass of
 *                this colorop is not possible
 * @return zero on success, -E value on failure
 */
int drm_colorop_mult_init(struct drm_device *dev, struct drm_colorop *colorop,
			      struct drm_plane *plane, bool allow_bypass)
{
	struct drm_property *prop;
	int ret;

	ret = drm_colorop_init(dev, colorop, plane, DRM_COLOROP_MULTIPLIER,
			       allow_bypass);
	if (ret)
		return ret;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC, "MULTIPLIER", 0, U64_MAX);
	if (!prop)
		return -ENOMEM;

	colorop->multiplier_property = prop;
	drm_object_attach_property(&colorop->base, colorop->multiplier_property, 0);

	drm_colorop_reset(colorop);

	return 0;
}
EXPORT_SYMBOL(drm_colorop_mult_init);

static void __drm_atomic_helper_colorop_duplicate_state(struct drm_colorop *colorop,
							struct drm_colorop_state *state)
{
	memcpy(state, colorop->state, sizeof(*state));

	state->bypass = true;
}

struct drm_colorop_state *
drm_atomic_helper_colorop_duplicate_state(struct drm_colorop *colorop)
{
	struct drm_colorop_state *state;

	if (WARN_ON(!colorop->state))
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_colorop_duplicate_state(colorop, state);

	return state;
}


void drm_colorop_atomic_destroy_state(struct drm_colorop *colorop,
				      struct drm_colorop_state *state)
{
	kfree(state);
}

/**
 * __drm_colorop_state_reset - resets colorop state to default values
 * @colorop_state: atomic colorop state, must not be NULL
 * @colorop: colorop object, must not be NULL
 *
 * Initializes the newly allocated @colorop_state with default
 * values. This is useful for drivers that subclass the CRTC state.
 */
static void __drm_colorop_state_reset(struct drm_colorop_state *colorop_state,
				      struct drm_colorop *colorop)
{
	u64 val;

	colorop_state->colorop = colorop;
	colorop_state->bypass = true;

	if (colorop->curve_1d_type_property) {
		drm_object_property_get_default_value(&colorop->base,
						colorop->curve_1d_type_property,
						&val);
		colorop_state->curve_1d_type = val;
	}
}

/**
 * __drm_colorop_reset - reset state on colorop
 * @colorop: drm colorop
 * @colorop_state: colorop state to assign
 *
 * Initializes the newly allocated @colorop_state and assigns it to
 * the &drm_crtc->state pointer of @colorop, usually required when
 * initializing the drivers or when called from the &drm_colorop_funcs.reset
 * hook.
 *
 * This is useful for drivers that subclass the colorop state.
 */
static void __drm_colorop_reset(struct drm_colorop *colorop,
				struct drm_colorop_state *colorop_state)
{
	if (colorop_state)
		__drm_colorop_state_reset(colorop_state, colorop);

	colorop->state = colorop_state;
}

void drm_colorop_reset(struct drm_colorop *colorop)
{
	kfree(colorop->state);
	colorop->state = kzalloc(sizeof(*colorop->state), GFP_KERNEL);

	if (colorop->state)
		__drm_colorop_reset(colorop, colorop->state);
}

static const char * const colorop_type_name[] = {
	[DRM_COLOROP_1D_CURVE] = "1D Curve",
	[DRM_COLOROP_1D_LUT] = "1D Curve Custom LUT",
	[DRM_COLOROP_CTM_3X4] = "3x4 Matrix",
	[DRM_COLOROP_MULTIPLIER] = "Multiplier",
};
static const char * const colorop_lut1d_interpolation_name[] = {
	[DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR] = "Linear",
};

const char *drm_get_colorop_type_name(enum drm_colorop_type type)
{
	if (WARN_ON(type >= ARRAY_SIZE(colorop_type_name)))
		return "unknown";

	return colorop_type_name[type];
}

const char *drm_get_colorop_curve_1d_type_name(enum drm_colorop_curve_1d_type type)
{
	if (WARN_ON(type >= ARRAY_SIZE(colorop_curve_1d_type_names)))
		return "unknown";

	return colorop_curve_1d_type_names[type];
}

/**
 * drm_get_colorop_lut1d_interpolation_name: return a string for interpolation type
 * @type: interpolation type to compute name of
 *
 * In contrast to the other drm_get_*_name functions this one here returns a
 * const pointer and hence is threadsafe.
 */
const char *drm_get_colorop_lut1d_interpolation_name(enum drm_colorop_lut1d_interpolation_type type)
{
	if (WARN_ON(type >= ARRAY_SIZE(colorop_lut1d_interpolation_name)))
		return "unknown";

	return colorop_lut1d_interpolation_name[type];
}

/**
 * drm_colorop_set_next_property - sets the next pointer
 * @colorop: drm colorop
 * @next: next colorop
 *
 * Should be used when constructing the color pipeline
 */
void drm_colorop_set_next_property(struct drm_colorop *colorop, struct drm_colorop *next)
{
	if (!colorop->next_property)
		return;

	drm_object_property_set_value(&colorop->base,
				      colorop->next_property,
				      next ? next->base.id : 0);
	colorop->next = next;
}
EXPORT_SYMBOL(drm_colorop_set_next_property);
