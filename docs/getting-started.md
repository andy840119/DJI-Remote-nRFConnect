# Getting Started

Everything needed to build and flash DJI-Remote-nRFConnect from a clean
machine.

---

## 1. Install the nRF Connect SDK

This project is built against **nRF Connect SDK v3.3.0**.

### Option A – nRF Connect for VS Code (recommended)

1. Install [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop)
2. Open the **Toolchain Manager** and install **nRF Connect SDK v3.3.0** — this
   installs both the SDK and the matching toolchain (Zephyr SDK, CMake, Ninja,
   Python, west)
3. Install the [nRF Connect for VS Code extension pack](https://marketplace.visualstudio.com/items?itemName=nordic-semiconductor.nrf-connect-extension-pack)

The default install location is `C:\ncs\v3.3.0` on Windows and
`~/ncs/v3.3.0` on Linux/macOS.

### Option B – command line

```bash
pip install west
west init -m https://github.com/nrfconnect/sdk-nrf --mr v3.3.0 ~/ncs
cd ~/ncs
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

You also need the [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html)
for the `arm-zephyr-eabi` toolchain.

---

## 2. Enter the build environment

The toolchain is not on `PATH` by default.

**VS Code:** open this folder, then *nRF Connect → Add build configuration*.
The extension sets the environment for you; skip to step 3.

**Windows command line:** open the toolchain's shell from the Toolchain Manager
("Open terminal"), or export the environment manually:

```bash
NCS_TOOLCHAIN=/c/ncs/toolchains/<hash>
export PATH="$NCS_TOOLCHAIN:$NCS_TOOLCHAIN/mingw64/bin:$NCS_TOOLCHAIN/bin:$NCS_TOOLCHAIN/opt/bin:$NCS_TOOLCHAIN/opt/bin/Scripts:$NCS_TOOLCHAIN/opt/zephyr-sdk/arm-zephyr-eabi/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$NCS_TOOLCHAIN/opt/zephyr-sdk"
export ZEPHYR_BASE=/c/ncs/v3.3.0/zephyr
```

**Linux/macOS:**

```bash
source ~/ncs/zephyr/zephyr-env.sh
```

Check it worked:

```bash
west --version
```

---

## 3. Build

```bash
cd DJI-Remote-nRFConnect
west build -b promicro_nrf52840/nrf52840/uf2 -p always
```

`-p always` forces a clean build; drop it for incremental builds.

Output lands in `build/DJI-Remote-nRFConnect/zephyr/`:

| File | Use |
|------|-----|
| `zephyr.uf2` | Drag-and-drop flashing through the Adafruit bootloader |
| `zephyr.hex` | Flashing with a debug probe |
| `zephyr.elf` | Debugging |

### Board targets

| Target | When to use |
|--------|-------------|
| `promicro_nrf52840/nrf52840/uf2` | Stock board with the Adafruit UF2 bootloader (default) |
| `promicro_nrf52840/nrf52840` | Board whose bootloader has been erased, flashed with a debug probe |

---

## 4. Flash

### With the UF2 bootloader (no probe needed)

1. Bridge **RST to GND twice in quick succession**. The status LED starts a
   fading pattern and a USB mass-storage device appears.
2. Copy `build/DJI-Remote-nRFConnect/zephyr/zephyr.uf2` onto that drive.
3. The board reboots into the firmware automatically.

### With a debug probe

```bash
west flash
```

Requires a J-Link or CMSIS-DAP probe on the SWD pads on the back of the board,
and the `promicro_nrf52840/nrf52840` board target.

---

## 5. View the logs

The board has no debug UART broken out, so the console runs over **USB CDC
ACM**. After the firmware boots, the board enumerates as a serial port:

| OS | Port |
|----|------|
| Windows | `COMx` (Device Manager → Ports) |
| Linux | `/dev/ttyACM0` |
| macOS | `/dev/tty.usbmodem*` |

Open it at any baud rate (CDC ignores it), for example:

```bash
minicom -D /dev/ttyACM0
```

Log output starts only after USB has enumerated, so the first few lines of the
boot sequence may be missing. Adjust verbosity with `CONFIG_DJI_REMOTE_LOG_LEVEL_*`
in `prj.conf`, or at runtime through `app_log_set_level()`.

---

## 6. Recovering a bricked board

If the UF2 bootloader itself is erased, reflash it over SWD with OpenOCD or a
J-Link — see the
[Zephyr board documentation](https://docs.zephyrproject.org/latest/boards/others/promicro_nrf52840/doc/index.html)
for the procedure.

---

## Troubleshooting

**`west: command not found`** — the toolchain environment is not exported; see
step 2.

**`Could not find zephyr package`** — `ZEPHYR_BASE` is unset or points at the
wrong SDK version.

**No USB serial port appears** — the firmware did not boot. Re-enter the
bootloader (double RST) and reflash.

**Nothing on the display** — check the SPI wiring against
[`docs/hardware-promicro-nrf52840.md`](hardware-promicro-nrf52840.md); the
panel is write-only, so bad wiring fails silently. The backlight only turns on
after the splash screen has been rendered.

**GPS never gets a fix** — verify the module's baud rate is 9600
(`CONFIG_GPS_UART_BAUD_RATE`) and that TX/RX are crossed: module TX goes to
P0.10, module RX to P0.09.
