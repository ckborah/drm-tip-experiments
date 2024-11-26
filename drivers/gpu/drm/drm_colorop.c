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

static const struct drm_prop_enum_list drm_colorop_type_enum_list[] = {
	{ DRM_COLOROP_1D_CURVE, "1D Curve" },
};

static const char * const colorop_curve_1d_type_names[] = {
	[DRM_COLOROP_1D_CURVE_SRGB_EOTF] = "sRGB EOTF",
	[DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF] = "sRGB Inverse EOTF",
};


/* Init Helpers */

static int drm_colorop_init(struct drm_device *dev, struct drm_colorop *colorop,
			    struct drm_plane *plane, enum drm_colorop_type type)
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
 * @return zero on success, -E value on failure
 */
int drm_colorop_curve_1d_init(struct drm_device *dev, struct drm_colorop *colorop,
			      struct drm_plane *plane, u64 supported_tfs)
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

	ret = drm_colorop_init(dev, colorop, plane, DRM_COLOROP_1D_CURVE);
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

static void __drm_atomic_helper_colorop_duplicate_state(struct drm_colorop *colorop,
							struct drm_colorop_state *state)
{
	memcpy(state, colorop->state, sizeof(*state));
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
