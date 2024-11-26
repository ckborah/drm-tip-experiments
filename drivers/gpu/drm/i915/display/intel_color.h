/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_COLOR_H__
#define __INTEL_COLOR_H__

#include <linux/types.h>

struct intel_atomic_state;
struct intel_crtc_state;
struct intel_crtc;
struct intel_display;
struct intel_dsb;
struct drm_property_blob;
struct drm_plane;
struct drm_prop_enum_list;
enum intel_color_block;

#define MAX_COLOR_PIPELINES 5

void intel_color_init_hooks(struct intel_display *display);
int intel_color_init(struct intel_display *display);
void intel_color_crtc_init(struct intel_crtc *crtc);
int intel_color_check(struct intel_atomic_state *state,
		      struct intel_crtc *crtc);
void intel_color_prepare_commit(struct intel_atomic_state *state,
				struct intel_crtc *crtc);
void intel_color_cleanup_commit(struct intel_crtc_state *crtc_state);
bool intel_color_uses_dsb(const struct intel_crtc_state *crtc_state);
void intel_color_wait_commit(const struct intel_crtc_state *crtc_state);
void intel_color_commit_noarm(struct intel_dsb *dsb,
			      const struct intel_crtc_state *crtc_state);
void intel_color_commit_arm(struct intel_dsb *dsb,
			    const struct intel_crtc_state *crtc_state);
void intel_color_post_update(const struct intel_crtc_state *crtc_state);
void intel_color_load_luts(const struct intel_crtc_state *crtc_state);
void intel_color_modeset(const struct intel_crtc_state *crtc_state);
void intel_color_get_config(struct intel_crtc_state *crtc_state);
bool intel_color_lut_equal(const struct intel_crtc_state *crtc_state,
			   const struct drm_property_blob *blob1,
			   const struct drm_property_blob *blob2,
			   bool is_pre_csc_lut);
void intel_color_assert_luts(const struct intel_crtc_state *crtc_state);
struct intel_plane_colorop *intel_colorop_alloc(void);
struct intel_plane_colorop *intel_plane_colorop_create(enum intel_color_block id);
int intel_plane_tf_pipeline_init(struct drm_plane *plane, struct drm_prop_enum_list *list);
int intel_plane_color_init(struct drm_plane *plane);

#endif /* __INTEL_COLOR_H__ */
