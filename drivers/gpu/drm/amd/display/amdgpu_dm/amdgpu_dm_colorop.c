// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include <drm/drm_print.h>
#include <drm/drm_plane.h>
#include <drm/drm_property.h>
#include <drm/drm_colorop.h>

#include "amdgpu.h"
#include "amdgpu_dm_colorop.h"

const u64 amdgpu_dm_supported_degam_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF);

const u64 amdgpu_dm_supported_shaper_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_OETF);

const u64 amdgpu_dm_supported_blnd_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF);

const struct drm_color_lut_range amdgpu_shaper_lut_range[] = {
	/* segment 1 */
	{
		.flags = (DRM_MODE_LUT_INTERPOLATE |
				DRM_MODE_LUT_NON_DECREASING),
		.count = 4096,
		.input_bpc = 24, .output_bpc = 16,
		.start = 0, .end = (1 << 16) - 1,
		.min = 0, .max = (1 << 16) - 1,
	},
};

const struct drm_color_lut_range amdgpu_blend_lut_range[] = {
	/* segment 1 */
	{
		.flags = (DRM_MODE_LUT_INTERPOLATE |
				DRM_MODE_LUT_NON_DECREASING),
		.count = 4096,
		.input_bpc = 24, .output_bpc = 16,
		.start = 0, .end = (1 << 16) - 1,
		.min = 0, .max = (1 << 16) - 1,
	},
};

int amdgpu_dm_initialize_default_pipeline(struct drm_plane *plane, struct drm_prop_enum_list *list)
{
	struct drm_colorop *op, *prev_op;
	struct drm_device *dev = plane->dev;
	int ret;

	/* 1D curve - DEGAM TF */
	op = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!op) {
		DRM_ERROR("KMS: Failed to allocate colorop\n");
		return -ENOMEM;
	}

	ret = drm_colorop_curve_1d_init(dev, op, plane, amdgpu_dm_supported_degam_tfs);
	if (ret)
		return ret;

	list->type = op->base.id;
	list->name = kasprintf(GFP_KERNEL, "Color Pipeline %d", op->base.id);

	prev_op = op;

	/* 3x4 matrix */
	op = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!op) {
		DRM_ERROR("KMS: Failed to allocate colorop\n");
		return -ENOMEM;
	}

	ret = drm_colorop_ctm_3x4_init(dev, op, plane);
	if (ret)
		return ret;

	drm_colorop_set_next_property(prev_op, op);

	prev_op = op;

	/* 1D curve - SHAPER TF */
	op = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!op) {
		DRM_ERROR("KMS: Failed to allocate colorop\n");
		return -ENOMEM;
	}

	ret = drm_colorop_curve_1d_init(dev, op, plane, amdgpu_dm_supported_shaper_tfs);
	if (ret)
		return ret;

	drm_colorop_set_next_property(prev_op, op);

	prev_op = op;

	/* 1D LUT - SHAPER LUT */
	op = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!op) {
		DRM_ERROR("KMS: Failed to allocate colorop\n");
		return -ENOMEM;
	}

	ret = drm_colorop_curve_1d_lut_init(dev, op, plane, amdgpu_shaper_lut_range,
					    sizeof(amdgpu_shaper_lut_range));
	if (ret)
		return ret;

	drm_colorop_set_next_property(prev_op, op);

	prev_op = op;

	/* 1D curve - BLND TF */
	op = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!op) {
		DRM_ERROR("KMS: Failed to allocate colorop\n");
		return -ENOMEM;
	}

	ret = drm_colorop_curve_1d_init(dev, op, plane, amdgpu_dm_supported_blnd_tfs);
	if (ret)
		return ret;

	drm_colorop_set_next_property(prev_op, op);

	prev_op = op;

	/* 1D LUT - BLND LUT */
	op = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!op) {
		DRM_ERROR("KMS: Failed to allocate colorop\n");
		return -ENOMEM;
	}

	ret = drm_colorop_curve_1d_lut_init(dev, op, plane, amdgpu_blend_lut_range,
					    sizeof(amdgpu_blend_lut_range));
	if (ret)
		return ret;

	drm_colorop_set_next_property(prev_op, op);
	return 0;
}
