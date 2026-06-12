# ms912x driver for Linux

Linux kernel driver for MacroSilicon USB to VGA/HDMI display adapters.

## Supported Hardware

| VID:PID    | Interface | Notes         |
|------------|-----------|---------------|
| `534d:6021` | USB 2.0   | Most common   |
| `345f:9132` | USB 3.0   |               |

## Kernel Compatibility

| Kernel Version | Notes                                          |
|----------------|------------------------------------------------|
| 6.11           | Works out of the box                           |
| 6.13+          | Timer API and fbdev API changes handled        |
| 6.17           | Tested on Ubuntu (6.17.0-29-generic)           |
| 7.0+           | Compatible — `drm_client_setup_with_fourcc` still available |

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

### Step 1: Install dependencies

On Debian/Ubuntu:

```bash
sudo apt install build-essential linux-headers-$(uname -r) dkms git
```

On Arch Linux:

```bash
sudo pacman -S base-devel linux-headers dkms git
```

### Step 2: Clone the repository

```bash
git clone https://github.com/gdnavas/ms912x.git
cd ms912x
```

### Step 3: Build and install the module

**Option A: DKMS (recommended)** — automatically rebuilds the module on kernel upgrades:

```bash
sudo dkms install .
```

**Option B: Manual build and load:**

```bash
make
sudo insmod ms912x.ko
```

Or use the provided script:

```bash
./insmod.sh
```

### Step 4: Auto-load on boot (optional)

To load the module automatically at every boot:

```bash
echo ms912x | sudo tee /etc/modules-load.d/ms912x.conf
```

### Step 5: Verify the driver is working

```bash
lsmod | grep ms912x
sudo modetest -M ms912x
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

## Verifying the Driver

After loading the module, check that the device is detected:

```bash
lsmod | grep ms912x
dmesg | tail -20
```

Check the display connector:

```bash
cat /sys/class/drm/card*-HDMI-A-*/status
```

Test mode setting:

```bash
modetest -M ms912x
```

## Known Limitations

### X11 PRIME does not work

On systems with an integrated GPU (Intel/AMD) as the primary display, the USB adapter is treated as a secondary GPU. Xorg attempts to create a PRIME shared pixmap to share the framebuffer between GPUs, but this fails with `ENOSPC` ("No space left on device") because the USB host controller's 32-bit DMA mask cannot map shmem pages allocated above 4GB.

This is a known limitation of the Xorg modesetting driver with USB display adapters — it affects all USB display solutions (ms912x, DisplayLink/udl, etc.), not just this driver.

**Workaround:** Use a Wayland compositor (e.g., GNOME on Wayland, Sway, KDE Plasma Wayland) instead of X11. Wayland handles multi-GPU output differently and does not rely on PRIME shared pixmaps.

## Development

Driver is developed by analyzing USB traffic captures (via Wireshark) from the device. Reverse engineering notes, register dumps, and resolution data are in the `re_notes/` directory.

## License

GPL-2.0 — see [LICENSE](LICENSE).
