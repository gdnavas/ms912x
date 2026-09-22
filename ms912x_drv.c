// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "ms912x.h"

static int ms912x_usb_suspend(struct usb_interface *interface,
			      pm_message_t message)
{
	struct drm_device *dev = usb_get_intfdata(interface);

	return drm_mode_config_helper_suspend(dev);
}

static int ms912x_usb_resume(struct usb_interface *interface)
{
	struct drm_device *dev = usb_get_intfdata(interface);

	return drm_mode_config_helper_resume(dev);
}

/*
 * FIXME: Dma-buf sharing requires DMA support by the importing device.
 *        This function is a workaround to make USB devices work as well.
 *        See todo.rst for how to fix the issue in the dma-buf framework.
 */
static struct drm_gem_object *
ms912x_driver_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	struct ms912x_device *ms912x = to_ms912x(dev);

	if (!ms912x->dmadev)
		return ERR_PTR(-ENODEV);

	return drm_gem_prime_import_dev(dev, dma_buf, ms912x->dmadev);
}

DEFINE_DRM_GEM_FOPS(ms912x_driver_fops);

static const struct drm_driver driver = {
	.driver_features = DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET | DRIVER_RENDER,

	.fops = &ms912x_driver_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.gem_prime_import = ms912x_driver_gem_prime_import,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static const struct ms912x_mode ms912x_mode_list[] = {
	/* Found in captures of the Windows driver */
	MS912X_MODE( 640,  480, 60, 0x40, MS912X_PIXFMT_UYVY),
	MS912X_MODE( 720,  480, 60, 0x02, MS912X_PIXFMT_UYVY),
	MS912X_MODE( 720,  576, 60, 0x11, MS912X_PIXFMT_UYVY),
	MS912X_MODE( 800,  600, 60, 0x42, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1024,  768, 60, 0x47, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1152,  864, 60, 0x4c, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  600, 60, 0x4e, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  720, 60, 0x4f, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  768, 60, 0x54, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  800, 60, 0x57, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  960, 60, 0x5b, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280, 1024, 60, 0x60, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1360,  768, 60, 0x64, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1366,  768, 60, 0x66, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1400, 1050, 60, 0x67, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1440,  900, 60, 0x6b, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1600, 1200, 60, 0x73, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1680, 1050, 60, 0x78, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1920, 1080, 60, 0x81, MS912X_PIXFMT_UYVY),
};

static const struct ms912x_mode *
ms912x_get_mode(const struct drm_display_mode *mode)
{
	int i;
	int width = mode->hdisplay;
	int height = mode->vdisplay;
	int hz = drm_mode_vrefresh(mode);

	for (i = 0; i < ARRAY_SIZE(ms912x_mode_list); i++) {
		if (ms912x_mode_list[i].width == width &&
		    ms912x_mode_list[i].height == height &&
		    ms912x_mode_list[i].hz == hz) {
			return &ms912x_mode_list[i];
		}
	}
	return NULL;
}

static const struct drm_mode_config_funcs ms912x_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs
ms912x_mode_config_helper_funcs = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static void ms912x_crtc_atomic_enable(struct drm_crtc *crtc,
				      struct drm_atomic_commit *state)
{
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_device *dev = crtc->dev;
	struct ms912x_device *ms912x = to_ms912x(dev);
	const struct ms912x_mode *m;
	int ret;

	ret = ms912x_power_on(ms912x);
	if (ret) {
		drm_err(dev, "failed to power on display: %d\n", ret);
		return;
	}

	m = ms912x_get_mode(&crtc_state->mode);
	if (!m) {
		drm_err(dev, "unsupported mode passed to CRTC enable\n");
		return;
	}

	ret = ms912x_set_resolution(ms912x, m);
	if (ret)
		drm_err(dev, "failed to set display mode: %d\n", ret);
}

static void ms912x_cancel_transfer_work(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		struct ms912x_usb_request *request = &ms912x->requests[i];

		if (cancel_work_sync(&request->work))
			complete(&request->done);
	}
}

static void ms912x_crtc_atomic_disable(struct drm_crtc *crtc,
				       struct drm_atomic_commit *state)
{
	struct drm_device *dev = crtc->dev;
	struct ms912x_device *ms912x = to_ms912x(dev);
	int ret;

	ms912x_cancel_transfer_work(ms912x);
	ret = ms912x_power_off(ms912x);
	if (ret && ret != -ENODEV)
		drm_err(dev, "failed to power off display: %d\n", ret);
}

static int ms912x_plane_atomic_check(struct drm_plane *plane,
				     struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state = NULL;

	if (new_plane_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   new_plane_state->crtc);

	return drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static void ms912x_plane_atomic_update(struct drm_plane *plane,
				       struct drm_atomic_commit *state)
{
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state =
		to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_rect rect;

	if (!fb)
		return;

	drm_rect_init(&rect, 0, 0, fb->width, fb->height);
	ms912x_fb_send_rect(fb, &shadow_plane_state->data[0], &rect);
}

static const struct drm_plane_funcs ms912x_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static const struct drm_plane_helper_funcs ms912x_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = ms912x_plane_atomic_check,
	.atomic_update = ms912x_plane_atomic_update,
};

static const struct drm_crtc_funcs ms912x_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs ms912x_crtc_helper_funcs = {
	.atomic_check = drm_crtc_helper_atomic_check,
	.atomic_enable = ms912x_crtc_atomic_enable,
	.atomic_disable = ms912x_crtc_atomic_disable,
};

static const struct drm_encoder_funcs ms912x_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const uint32_t ms912x_pipe_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static int ms912x_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	int ret;
	struct ms912x_device *ms912x;
	struct drm_device *dev;

	ms912x = devm_drm_dev_alloc(&interface->dev, &driver,
				    struct ms912x_device, drm);
	if (IS_ERR(ms912x))
		return PTR_ERR(ms912x);

	ms912x->intf = interface;
	dev = &ms912x->drm;

	ms912x->dmadev = usb_intf_get_dma_device(interface);
	if (!ms912x->dmadev)
		drm_warn(dev,
			 "buffer sharing not supported"); /* not an error */

	ret = drmm_mode_config_init(dev);
	if (ret)
		goto err_put_device;

	dev->mode_config.min_width = 0;
	dev->mode_config.max_width = 2048;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_height = 2048;
	dev->mode_config.funcs = &ms912x_mode_config_funcs;
	dev->mode_config.helper_private = &ms912x_mode_config_helper_funcs;

	ret = drm_vblank_init(dev, 1);
	if (ret)
		goto err_put_device;

	/* This stops weird behavior in the device */
	ms912x_set_resolution(ms912x, &ms912x_mode_list[0]);

	ret = ms912x_init_request(ms912x, &ms912x->requests[0],
				  2048 * 2048 * 2);
	if (ret)
		goto err_put_device;

	ret = ms912x_init_request(ms912x, &ms912x->requests[1],
				  2048 * 2048 * 2);
	if (ret)
		goto err_free_request_0;
	complete(&ms912x->requests[1].done);

	ret = ms912x_connector_init(ms912x);
	if (ret)
		goto err_free_request_1;

	ret = drm_universal_plane_init(&ms912x->drm, &ms912x->primary_plane, 0,
				       &ms912x_plane_funcs,
				       ms912x_pipe_formats,
				       ARRAY_SIZE(ms912x_pipe_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		goto err_free_request_1;

	drm_plane_helper_add(&ms912x->primary_plane, &ms912x_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(&ms912x->primary_plane);

	ret = drm_crtc_init_with_planes(&ms912x->drm, &ms912x->crtc,
					 &ms912x->primary_plane, NULL,
					 &ms912x_crtc_funcs, NULL);
	if (ret)
		goto err_free_request_1;

	drm_crtc_helper_add(&ms912x->crtc, &ms912x_crtc_helper_funcs);

	ret = drm_encoder_init(&ms912x->drm, &ms912x->encoder,
			       &ms912x_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		goto err_free_request_1;

	ms912x->encoder.possible_crtcs = drm_crtc_mask(&ms912x->crtc);

	ret = drm_connector_attach_encoder(&ms912x->connector, &ms912x->encoder);
	if (ret)
		goto err_free_request_1;

	ms912x->primary_plane.possible_crtcs = drm_crtc_mask(&ms912x->crtc);

	drm_mode_config_reset(dev);

	usb_set_intfdata(interface, ms912x);

	drmm_kms_helper_poll_init(dev);

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free_request_1;

	drm_client_setup_with_fourcc(dev, DRM_FORMAT_XRGB8888);

	return 0;

err_free_request_1:
	ms912x_free_request(&ms912x->requests[1]);
err_free_request_0:
	ms912x_free_request(&ms912x->requests[0]);
err_put_device:
	put_device(ms912x->dmadev);
	return ret;
}

static void ms912x_usb_disconnect(struct usb_interface *interface)
{
	struct ms912x_device *ms912x = usb_get_intfdata(interface);
	struct drm_device *dev = &ms912x->drm;

	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
	ms912x_cancel_transfer_work(ms912x);
	ms912x_free_request(&ms912x->requests[0]);
	ms912x_free_request(&ms912x->requests[1]);
	put_device(ms912x->dmadev);
	ms912x->dmadev = NULL;
}

static const struct usb_device_id id_table[] = {
	/* USB 2 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x534d, 0x6021, 0xff, 0x00, 0x00) },
	/* USB 2 Sometimes this PID will pop up*/
	{ USB_DEVICE_AND_INTERFACE_INFO(0x534d, 0x0821, 0xff, 0x00, 0x00) },
	/* USB 3 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x345f, 0x9132, 0xff, 0x00, 0x00) },
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver ms912x_driver = {
	.name = "ms912x",
	.probe = ms912x_usb_probe,
	.disconnect = ms912x_usb_disconnect,
	.suspend = ms912x_usb_suspend,
	.resume = ms912x_usb_resume,
	.id_table = id_table,
};
module_usb_driver(ms912x_driver);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
