# ms912x driver for Linux

Linux kernel driver for MacroSilicon USB to VGA/HDMI display adapters.

## Supported Hardware

| VID:PID     | Interface | Notes            |
|-------------|-----------|------------------|
| `534d:6021` | USB 2.0   | Most common      |
| `345f:9132` | USB 3.0   | MacroSilicon MS912x |

## Kernel Compatibility

| Kernel Version | Notes                                                     |
|----------------|-----------------------------------------------------------|
| 6.11           | Works out of the box                                      |
| 6.13+          | Timer API and fbdev API changes handled                   |
| 6.17           | Tested on Ubuntu (6.17.0-29-generic)                      |
| 7.0+           | Compatible — uses full atomic helpers                     |

## Prerequisites

- Linux kernel headers matching your running kernel
- `make`, `gcc` (or `clang`)
- `dkms` (optional, for automatic rebuild on kernel upgrades)

On Debian/Ubuntu:

```bash
sudo apt install build-essential linux-headers-$(uname -r) dkms
```

On Arch Linux:

```bash
sudo pacman -S base-devel linux-headers dkms
```

## Installation

### Step 1: Clone the repository

```bash
git clone https://github.com/gdnavas/ms912x.git
cd ms912x
```

### Step 2: Build and install the module

**Option A: DKMS (recommended)** — automatically rebuilds the module on kernel upgrades:

```bash
sudo dkms install .
```

**Option B: Manual build and load:**

```bash
make CC=clang LD=ld.lld
sudo insmod ms912x.ko
```

Or use the provided script:

```bash
./insmod.sh
```

### Step 3: Auto-load on boot (optional)

```bash
echo ms912x | sudo tee /etc/modules-load.d/ms912x.conf
```

### Step 4: Verify the driver is working

```bash
lsmod | grep ms912x
dmesg | tail -20
cat /sys/class/drm/card*-HDMI-A-*/status
```

### Uninstall

If installed via DKMS:

```bash
sudo dkms remove ms912x/0.1 --all
```

If loaded manually:

```bash
sudo rmmod ms912x
```

## Compositor Support

### X11 (Xorg)

**Does not work for multi-monitor.** On systems with an integrated GPU as primary, the USB adapter is treated as a secondary GPU. Xorg attempts PRIME shared pixmaps, which fail with `ENOSPC` because the USB host controller's 32-bit DMA mask cannot map shmem pages above 4GB. This affects all USB display solutions, not just this driver.

**Workaround:** Use a Wayland compositor instead.

### Wayland Compositors

The driver registers as a full DRM device with atomic modesetting, CRTC, encoder, and primary plane. Wayland compositors detect it as an output via hotplug. However, support varies:

| Compositor    | Status    | Notes                                                      |
|---------------|-----------|------------------------------------------------------------|
| SDDM (login)  | Works     | fbdev emulation shows on the USB display                   |
| Hyprland      | Limited   | See [Hyprland Limitations](#hyprland-limitations) below   |
| Sway/wlroots  | Untested  | May work — uses similar DRM output path as Hyprland       |
| GNOME Wayland | Untested  | May have better multi-GPU handling                         |

### Hyprland Limitations

Hyprland uses **aquamarine** as its DRM backend. Aquamarine enumerates all DRM devices and attempts to create an EGL renderer on each GPU via Mesa. The ms912x adapter has no Mesa DRI driver (`ms912x_dri.so` does not exist), so aquamarine's `initMgpu()` fails with:

```
CDRMRenderer: fail, eglInitialize failed
drm: initMgpu: no renderer
```

This causes the output to be marked as disabled — the monitor is detected, modes are read via EDID, a CRTC is assigned, but the output never becomes active.

**Why this happens:**

1. Aquamarine calls `CDRMRenderer::attempt()` on the ms912x card node
2. Mesa attempts DRI2 driver load for "ms912x" → fails (no matching driver)
3. `eglInitialize()` returns `EGL_NOT_INITIALIZED`
4. Without a renderer, aquamarine cannot create framebuffers or blit frames to the output
5. Even though the ms912x driver supports DMA-BUF import (`gem_prime_import`), aquamarine never reaches the blit path because the renderer initialization fails first

**What was tried:**

- Patching aquamarine to treat `ms912x` the same as `evdi` (DisplayLink): set `rendererRequired=false` and `primary={}`. This skips renderer creation but the output still doesn't work because:
  - Without a primary GPU reference, aquamarine treats the device as standalone
  - Without a renderer, it cannot create framebuffers for compositing
  - The display receives no frames and stays black

- The fundamental issue is architectural: aquamarine requires a working EGL renderer on every GPU that has active outputs. This is not something the kernel driver can solve — it requires either:
  - A Mesa DRI driver for the MacroSilicon chip (significant reverse-engineering effort)
  - Aquamarine modifications to use the primary GPU's renderer for cross-device blitting (complex, would need upstream cooperation)
  - A different compositor with simpler multi-GPU handling

**Partial workarounds:**

- **SDDM login screen** works via fbdev emulation (early boot console is visible on the USB display)
- **TTY console** works via fbdev (`echo test > /dev/ttyUSB` or switch to the fbdev VT)
- **Console mode** with `modetest` can drive the display directly without a compositor

## What the Driver Does Well

Despite the compositor limitations, the driver itself is functional:

- ✅ EDID detection over USB (monitor is detected, modes are read)
- ✅ Atomic modesetting (CRTC, encoder, connector, primary plane)
- ✅ DRM dumb buffer allocation and scanning
- ✅ DMA-BUF import (for potential cross-GPU blitting)
- ✅ fbdev emulation for console/SDDM
- ✅ Multi-resolution support (640×480 through 1920×1080)
- ✅ RGB-to-YUV422 conversion for USB bulk transfer

## Development

Driver is developed by analyzing USB traffic captures (via Wireshark) from the device. Reverse engineering notes, register dumps, and resolution data are in the `re_notes/` directory.

## License

GPL-2.0 — see [LICENSE](LICENSE).
