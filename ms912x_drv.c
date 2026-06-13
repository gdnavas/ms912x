// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fbdev_ttm.h>
#include <drm/clients/drm_client_setup.h>
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

static int ms912x_driver_open(struct inode *inode, struct file *filp)
{
	int ret;

	pr_err("ms912x: device opened, minor=%d\n", iminor(inode));
	ret = drm_open(inode, filp);
	if (ret)
		pr_err("ms912x: drm_open failed with %d\n", ret);
	return ret;
}

static const struct file_operations ms912x_driver_fops = {
	.owner = THIS_MODULE,
	.open = ms912x_driver_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.compat_ioctl = drm_compat_ioctl,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static const struct drm_driver driver = {
	.driver_features = DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET | DRIVER_RENDER,

	/* GEM hooks */
	.fops = &ms912x_driver_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_TTM_DRIVER_OPS,
	.gem_prime_import = ms912x_driver_gem_prime_import,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static const struct ms912x_mode ms912x_mode_list[] = {
	/* Found in captures of the Windows driver */
	MS912X_MODE( 800,  600, 60, 0x4200, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1024,  768, 60, 0x4700, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1152,  864, 60, 0x4c00, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  720, 60, 0x4f00, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  800, 60, 0x5700, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  960, 60, 0x5b00, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280, 1024, 60, 0x6000, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1366,  768, 60, 0x6600, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1400, 1050, 60, 0x6700, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1440,  900, 60, 0x6b00, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1680, 1050, 60, 0x7800, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1920, 1080, 60, 0x8100, MS912X_PIXFMT_UYVY),

	/* Dumped from the device */
	MS912X_MODE( 720,  480, 60, 0x0200, MS912X_PIXFMT_UYVY),
	MS912X_MODE( 720,  576, 60, 0x1100, MS912X_PIXFMT_UYVY),
	MS912X_MODE( 640,  480, 60, 0x4000, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1024,  768, 60, 0x4900, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  600, 60, 0x4e00, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280,  768, 60, 0x5400, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1280, 1024, 60, 0x6100, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1360,  768, 60, 0x6400, MS912X_PIXFMT_UYVY),
	MS912X_MODE(1600, 1200, 60, 0x7300, MS912X_PIXFMT_UYVY),
	/* TODO: more mode numbers? */
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
	return ERR_PTR(-EINVAL);
}

static enum drm_mode_status
ms912x_mode_valid(struct drm_device *dev,
		  const struct drm_display_mode *mode)
{
	const struct ms912x_mode *m = ms912x_get_mode(mode);

	if (IS_ERR(m))
		return MODE_BAD;

	return MODE_OK;
}

static int ms912x_atomic_commit(struct drm_device *dev,
				struct drm_atomic_state *state,
				bool nonblock)
{
	pr_err("ms912x: atomic_commit nonblock=%d\n", nonblock);
	return drm_atomic_helper_commit(dev, state, nonblock);
}

static const struct drm_mode_config_funcs ms912x_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.mode_valid = ms912x_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = ms912x_atomic_commit,
};

static void ms912x_crtc_enable(struct drm_crtc *crtc,
			       struct drm_atomic_state *state)
{
	struct ms912x_device *ms912x = to_ms912x(crtc->dev);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_display_mode *mode = &crtc_state->mode;
	const struct ms912x_mode *m;

	pr_err("ms912x: crtc_enable %dx%d@%d\n",
	       mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode));

	ms912x_power_on(ms912x);

	m = ms912x_get_mode(mode);
	if (IS_ERR(m)) {
		pr_err("ms912x: no matching mode for %dx%d@%d\n",
		       mode->hdisplay, mode->vdisplay,
		       drm_mode_vrefresh(mode));
		return;
	}
	ms912x_set_resolution(ms912x, m);
}

static void ms912x_crtc_disable(struct drm_crtc *crtc,
				struct drm_atomic_state *state)
{
	struct ms912x_device *ms912x = to_ms912x(crtc->dev);

	pr_err("ms912x: crtc_disable\n");
	ms912x_power_off(ms912x);
}

static int ms912x_plane_check(struct drm_plane *plane,
			      struct drm_atomic_state *state)
{
	pr_err("ms912x: plane_check\n");
	return 0;
}

static void ms912x_plane_update(struct drm_plane *plane,
				struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state =
		to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_rect rect;
	struct ms912x_device *ms912x;

	if (!fb)
		return;

	ms912x = to_ms912x(fb->dev);

	pr_err("ms912x: plane_update %dx%d pitch=%d\n",
	       fb->width, fb->height, fb->pitches[0]);

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
	.atomic_check = ms912x_plane_check,
	.atomic_update = ms912x_plane_update,
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
	.atomic_enable = ms912x_crtc_enable,
	.atomic_disable = ms912x_crtc_disable,
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
	pr_err("ms912x: plane possible_crtcs=0x%lx crtc_mask=0x%lx\n",
	       ms912x->primary_plane.possible_crtcs,
	       drm_crtc_mask(&ms912x->crtc));

	drm_mode_config_reset(dev);
	pr_err("ms912x: after reset plane possible_crtcs=0x%lx\n",
	       ms912x->primary_plane.possible_crtcs);

	usb_set_intfdata(interface, ms912x);

	drm_kms_helper_poll_init(dev);

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

	cancel_work_sync(&ms912x->requests[0].work);
	cancel_work_sync(&ms912x->requests[1].work);
	drm_kms_helper_poll_fini(dev);
	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
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
MODULE_LICENSE("GPL");
