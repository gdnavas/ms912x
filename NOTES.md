# Development Notes

## Bugs Fixed

### 1. FOP_UNSIGNED_OFFSET (CRITICAL)

**Symptom:** `open("/dev/dri/card0")` returns `EINVAL` (errno 22), even as root.

**Cause:** Kernel 7.0's `drm_open_helper()` checks `filp->f_op->fop_flags & FOP_UNSIGNED_OFFSET`. The original driver used `DEFINE_DRM_GEM_FOPS()` which didn't set this flag (required since kernel ~6.9).

**Fix:** Replace `DEFINE_DRM_GEM_FOPS(ms912x_driver_fops)` with an explicit `struct file_operations` that includes `.fop_flags = FOP_UNSIGNED_OFFSET`.

### 2. DRIVER_RENDER — render node for Mesa/swrast

**Symptom:** Hyprland/aquamarine cannot create an EGL renderer for the ms912x output. Error: `eglQueryDeviceStringEXT errored out with EGL_BAD_PARAMETER`.

**Cause:** The ms912x adapter has no GPU. Without `DRIVER_RENDER`, no `/dev/dri/renderD*` node is created. Mesa needs a render node to initialize a software renderer.

**Fix:** Add `DRIVER_RENDER` to `driver_features`. The DRM core automatically creates `renderD130` with shmem-backed dumb buffer support via `DRM_GEM_SHMEM_DRIVER_OPS`.

### 3. drm_vblank_init()

**Symptom:** Page-flip completion events not delivered to userspace. Aquamarine: "Cannot commit when a page-flip is awaiting".

**Cause:** The USB adapter has no hardware vblank interrupts. Without `drm_vblank_init()`, the vblank infrastructure isn't initialized, so `drm_crtc_send_vblank_event()` can't deliver flip completion events from `drm_atomic_helper_commit_hw_done()`.

**Fix:** Add `drm_vblank_init(dev, 1)` after `drmm_mode_config_init()`.

### 4. Mode codes shortened

Original mode codes were 16-bit (e.g. `0x4200`). Dumped and verified they are actually 8-bit (`0x42`). Fixed all entries in `ms912x_mode_list`.

### 5. Removed debug `pr_err` noise

Replaced verbose `pr_err()` debug prints with proper `drm_err()`/`drm_info()` calls. Removed `ms912x_driver_open()` wrapper (unnecessary), restored `DEFINE_DRM_GEM_FOPS()`.

### 6. mode_config_helper_funcs

Added `ms912x_mode_config_helper_funcs` with `drm_atomic_helper_commit_tail_rpm` to properly handle the commit tail. Without this, the atomic commit path doesn't correctly clean up after commit completion.

### 7. CRTC/plane function signatures

Updated to match kernel 7.x signatures: `atomic_enable`/`atomic_disable` now receive `struct drm_atomic_commit *` instead of `struct drm_atomic_state *`. Plane functions similarly updated.

### 8. DRM_FBDEV_TTM → DRM_FBDEV_SHMEM

Replaced deprecated `DRM_FBDEV_TTM_DRIVER_OPS` with `DRM_FBDEV_SHMEM_DRIVER_OPS` to match the shmem-based GEM backend.

## Hyprland/Aquamarine Integration Investigation

### Root Cause: No Mesa DRI Driver for ms912x

Aquamarine (Hyprland's DRM backend) requires a working EGL renderer on every GPU that has active outputs. The renderer is created via Mesa's EGL/GBM stack, which needs a DRI driver matching the DRM device name.

For the ms912x device, Mesa finds no matching DRI driver:
```
ERR: [EGL] Command eglInitialize errored out with EGL_NOT_INITIALIZED (0x12289): DRI2: failed to load driver
ERR: CDRMRenderer: fail, eglInitialize failed
```

Without a renderer, aquamarine cannot:
1. Create GBM buffers for compositing
2. Render frames to the output
3. Blit from the primary GPU to the USB display

### What Aquamarine Does Detect

The driver correctly sets up the DRM device. Aquamarine successfully:
- Enumerates card2 via udev
- Opens it via libseat
- Registers it as a GPU with driver "ms912x"
- Detects the HDMI-A-2 connector as connected
- Reads EDID and parses supported modes (1920×1080@60Hz)
- Assigns CRTC and encoder
- Starts modesetting

The failure happens ONLY at the renderer creation step.

### Attempted Fix: evdi-Style Handling

Aquamarine has special handling for the `evdi` (DisplayLink) driver:
```cpp
if (driver == AQ_BACKEND_GPU_DRIVER_EVDI) {
    primary = {};
    rendererRequired = false;
}
```

This was extended to recognize `ms912x`:
```cpp
if (name == "ms912x")
    return AQ_BACKEND_GPU_DRIVER_EVDI;
```

**Result:** While this prevents the renderer creation error, it doesn't solve the problem because:
- Setting `primary = {}` removes the reference to the primary GPU
- Without a primary reference, `shouldBlit()` returns false
- The compositor tries to render directly on the ms912x device
- Without a renderer, no frames can be composed or submitted
- The display stays black

### Udev Seat Tags (SDDM/libseat Issue)

USB DRM devices created by the ms912x driver don't receive the `seat` tag from udev by default (unlike PCI devices). This prevents logind/libseat from taking the device:

```
ERR: libseat: Couldn't open device at /dev/dri/card2
ERR: drm: Skipping device .../card2, not a KMS device
```

**Workaround:** Add a udev rule:
```
# /etc/udev/rules.d/99-ms912x-seat.rules
ACTION!="remove", SUBSYSTEM=="drm", ATTRS{idVendor}=="345f", \
  ENV{ID-seat}="seat0", TAG+="seat", TAG+="master-of-seat", TAG+="uaccess"
```

This is NOT included in the driver itself because it requires a system-level configuration change. Users who want to use the adapter with Hyprland should add this rule.

### Path Forward

The Hyprland limitation cannot be solved at the kernel driver level. Options:

1. **Mesa DRI driver** — Would require reverse-engineering the MacroSilicon chip's rendering pipeline. The chip is primarily a display encoder (USB-to-HDMI bridge), not a GPU, so a DRI driver may not be practical.

2. **Aquamarine patch** — Could be modified to use the primary GPU's renderer for cross-GPU blitting when the secondary GPU has no renderer. This would require:
   - Skipping `initMgpu()` for devices recognized as USB display adapters
   - Modifying the blit path to use the primary GPU's renderer exclusively
   - Using DMA-BUF import on the secondary device for scanout
   - This is an upstream Hyprland/aquamarine change

3. **Alternative compositor** — Sway (wlroots) or other compositors may handle multi-GPU differently and might not require a renderer on secondary devices.

4. **fbdev-only usage** — The driver works for console/SDDM output via fbdev emulation. Users who only need early-boot display can use this mode.

## Known Issues

### Hotplug Storm

The connector detect function reads the EDID over USB every ~10 seconds via hotplug polling. This can saturate the system with I/O operations.

**Fixes:**
- Disable hotplug polling: don't call `drm_kms_helper_poll_init()`, or call `drm_kms_helper_poll_disable()` after init
- Add debouncing to `ms912x_detect()` (cache EDID, rate-limit USB reads)
- Use `connector_status_connected` permanently once initial detection succeeds

### "Cannot commit when a page-flip is awaiting"

Even with `drm_vblank_init()`, this error persists during early boot. The fbdev client does a blocking atomic commit that creates a pending flip event. Aquamarine sees this stale state and refuses to commit.

### USB Device State Corruption

If the adapter is disconnected while the module is loaded, the module's refcount can go to -1 (corrupt). The DRM device node persists in `/dev/dri/` but returns `ENODEV` on open. The module cannot be unloaded (`rmmod` fails with "Device or resource busy") and the adapter must be physically re-plugged to recover.

## System Info

- CachyOS Linux, kernel 7.2.5-1-cachyos (built with clang)
- 3 GPUs: card0=Intel iGPU, card1=Intel eDP, card2=ms912x USB
- Hyprland 0.56.2 with aquamarine 0.15.0 (DMS/Dank Material Shell)
