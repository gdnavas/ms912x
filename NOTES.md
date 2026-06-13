# Development Notes: Hyprland/Wayland Support (kernel 7.0)

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

## Remaining Issues

### Hotplug Storm

The connector detect function (`ms912x_detect`) reads the EDID over USB every ~10 seconds via hotplug polling. This causes repeated device open/close cycles that can saturate the system with hundreds of I/O operations per second. On some setups this causes:
- System slowdown
- Double-clicking behavior
- Audio dropout
- Input lag

**Possible fixes:**
- Disable hotplug polling: `drm_kms_helper_poll_init(dev)` → don't call it, or call `drm_kms_helper_poll_disable()` after init
- Add debouncing to `ms912x_detect()` (cache EDID, rate-limit USB reads)
- Use `connector_status_connected` permanently once initial detection succeeds

### Aquamarine "Cannot commit when a page-flip is awaiting"

Even with `drm_vblank_init()`, this error persists during early boot. The fbdev client (`drm_client_setup_with_fourcc`) does a blocking atomic commit that sets `crtc_state->event` via `drm_atomic_helper_setup_commit()`. This creates a `drm_crtc_commit` on `crtc->commit_list` whose `flip_done` completion may not be fully cleaned up before Aquamarine's first commit attempt.

The error occurs in Aquamarine's own code (not the kernel) — it tracks `pendingPageFlip` state per output and refuses to commit while a flip is pending.

**Root cause chain:**
1. `drm_client_setup_with_fourcc` → `drm_client_modeset_commit` → `drm_atomic_commit(nonblock=false)`
2. `drm_atomic_helper_setup_commit()` allocates `crtc_state->event` even for blocking commits
3. `drm_atomic_helper_wait_for_vblanks()` may not properly clean up on adapters without real vblank HW
4. Aquamarine sees stale pending flip state

### No EGL Renderer on card0

Aquamarine tries to create an EGL context on `/dev/dri/card0` (ms912x primary node) for blitting. Mesa's EGL attempts DRI2 driver load for "ms912x" → fails (no `ms912x_dri.so` exists). Even though `renderD130` exists, Aquamarine uses the primary card for EGL, not the render node.

This is an Aquamarine/multi-GPU design limitation — it should use the primary GPU's renderer (Intel renderD129) for cross-device blitting, but currently doesn't for non-recognized DRM drivers.

## Atomic Helpers Migration

The driver was migrated from the deprecated `drm_simple_display_pipe` to full atomic helpers:
- `drm_universal_plane_init()` for the primary plane
- `drm_crtc_init_with_planes()` for the CRTC
- `drm_encoder_init()` for the TMDS encoder
- Manual `possible_crtcs` assignment after both plane and CRTC are initialized

This was necessary because `drm_simple_display_pipe` creates an internal pipe that doesn't properly dispatch `atomic_update` when a compositor takes over.

## Test Programs

- `test_open.c` — Tests basic DRM device open + master acquire
- `test_atomic.c` — Full modeset test: creates FB, sets CRTC, fills with gradient pattern
- `reload.sh` — Module reload script for TTY2-based testing

## System Info

- Arch Linux, kernel 7.0.9-arch2-1
- 3 GPUs: card1=nvidia, card2=intel iGPU, card3=ms912x USB
- Hyprland (Wayland) via SDDM auto-login
- User password: `090720`
