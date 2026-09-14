---

# Layer Architecture: `meta-infusion-pump` 🧠

This document details the internal structure of the Argus Infusion Pump meta-layer. Each directory has a specific and critical responsibility to ensure the operation of the IoT Gateway, data security, and OTA (Over-The-Air) updates.

## 📂 Directory Overview (`recipes-*`)

### 1. `conf/` (Layer Configurations)

Where the fundamental rules of the layer are defined.

* **`layer.conf`**: Registers the layer within the Yocto ecosystem and defines its dependencies.
* **`argus-version.conf`**: Manages the global firmware versioning (`ARGUS_VERSION_STRING`), which is injected into both the final image and the RAUC update packages.

### 2. `recipes-apps/` (The Brain of the Gateway)

Contains User Space applications. Here resides **`argus-control-cpp`**, the custom-built C++ daemon for the pump.

* **`src/hal/` (Hardware Abstraction Layer):** Code that interacts directly with the Raspberry Pi pins (GPIO, I2C, PWM, SPI).
* **`src/drivers/`**: Bridge communication logic (`stm32_bridge.cpp`) with the STM32 microcontroller.
* **`src/services/` & `src/server_layer/**`: Infusion management and secure MQTT communication with the Backend.
* **`src/update_dir/`**: Handles OTA update commands internally.
* **`infusion-pump.service`**: Systemd file that ensures the daemon starts automatically on boot and restarts in case of failure.

### 3. `recipes-bsp/` (Board Support Package)

Low-level configurations for the Bootloader.

* **`u-boot/`**: Modified scripts (`boot.cmd`) that teach U-Boot how to communicate with RAUC. This script decides whether the system should boot partition A or partition B based on the status of the last update.

### 4. `recipes-connectivity/` (Network and Security)

Everything that connects the pump to the outside world.

* **`mosquitto/`**: Local MQTT Broker configuration. Includes access control policies (`acl`), cryptographic keys for mTLS (`certs/`), and systemd overrides.
* **`wpa-supplicant/`**: Wi-Fi configuration files (`wpa_supplicant.conf-sane`).
* **`avahi/`**: mDNS daemon configuration, allowing the pump to be discovered on the local network as `infusion-gateway.local`.
* **`boost-mqtt5/`**: Recipe to compile the advanced MQTT library used in the C++ code.

### 5. `recipes-core/` (The Core of the Linux System)

The backbone of the operating system image.

* **`images/`**:
* `infusion-image.bb`: Defines all packages that will be installed in the RootFS (e.g., networking packages, C++ libraries, debugging tools).
* `infusion-ab.wks`: *Wic* configuration. Defines the SD card partitioning (Boot, RootFS A, RootFS B, Data).


* **`bundles/`**: Logic to package the RootFS into an encrypted `.raucb` file for OTA updates. Contains the development certificates used to sign the update.
* **`systemd/` & `base-files/**`: Network tweaks (`25-wlan.network`), mount points (`fstab`) for the read-only file system, and the watchdog (to reboot the board if the system hangs).

### 6. `recipes-kernel/` (The Heart of the Hardware)

Tweaks applied directly to the Raspberry Pi Linux Kernel.

* **`linux/`**: Configuration fragments (`.cfg`).
* `pwm-cdev.cfg`: Enables user-space access to hardware PWM pins.
* `rauc-kernel.cfg`: Enables essential features for RAUC to function, such as support for SquashFS files and loopback devices.



### 7. `recipes-support/` (Base Tools)

Auxiliary systems running in the background.

* **`rauc/`**: Configuration of the OTA update daemon on the device. Contains the `system.conf` file (which describes the A/B partitioning of the board) and the public keyring (`keyring.pem`) used to validate if an update package is authentic and signed by you.

---

