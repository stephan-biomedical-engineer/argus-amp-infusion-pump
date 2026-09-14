---

# Argus Medical Control Infusion Pump Gateway Project

This is the Yocto Project meta-layer developed for the **Argus Infusion Pump IoT Gateway**. 
The system is based on a custom Embedded Linux environment for the **Raspberry Pi Zero 2 W** board, strictly designed with a focus on **Medical Cybersecurity**, **High Availability**, and **Hardware Integration**.

Developed for the Biomedical Instrumentation II course - Federal University of Uberlândia (UFU).

## ✨ Architecture & Key Features

This project implements medical industry engineering standards, moving beyond a simple academic prototype:

* **Embedded DevOps (OTA & A/B Partitioning):** Full integration with **RAUC**. The system features two root partitions (A/B) with automatic fallback support. Firmware updates are performed via signed bundles ensuring *Zero-Downtime*.
* **Network Security (mTLS):** MQTT Broker (Mosquitto) configured exclusively for TLS traffic (port 8883) with **Mutual Authentication (mTLS)**. Only clients with authorized certificates can send or receive telemetry.
* **Immutable File System:** Image generated with a `read-only-rootfs` and **DM-Verity**, protecting the gateway against SD card corruption and mitigating malware injection.
* **Service Discovery (mDNS):** Utilization of **Avahi** daemon (`argus-pump.local`) for dynamic provisioning on hospital networks without static IPs.
* **C++ Control Layer (`argus-control-cpp`):** Custom daemon using **Boost.Asio** and **libgpiod v2** that manages MQTT communication and acts as a high-speed binary bridge to the STM32 core.
* **Footprint Optimization:** Fully functional industrial Linux image in **~200MB**.



## 📂 Layer Structure

* `recipes-apps/`: Source code and compilation for the C++ daemon.
* `recipes-bsp/`: Modified **U-Boot** scripts for RAUC slot switching and fallback logic.
* `recipes-connectivity/`: Network configs (WPA Supplicant, Mosquitto mTLS/ACLs, and Avahi).
* `recipes-core/`: Base system, `systemd` watchdog configs, and final image/bundle recipes.
* `recipes-kernel/`: Kernel fragments (`.cfg`) for **PWM CDEV**, SPI/I2C, and DM-Verity support.
* `recipes-support/`: Configurations and public keyring for the RAUC update client.

## 📦 Dependencies

This layer (`scarthgap` branch) requires:
* `poky`
* `meta-openembedded`
* `meta-raspberrypi`
* `meta-rauc`

## 🔐 Key and Certificate Management (IMPORTANT)

For security, **private keys and certificates are NOT versioned in this repository**. 
To build the image, you must generate development certificates. We provide a helper script:

```bash
# Generate dummy certificates for development
cd meta-infusion-pump
./generate_dev_certs.sh
```

This script will populate the necessary directories in `recipes-connectivity/mosquitto/`, `recipes-support/rauc/`, and `recipes-core/bundles/` with the required `.pem`, `.crt`, and `.key` files.

## 🚀 How to Build

1. **Initialize the environment:**
   ```bash
   source poky/oe-init-build-env
   ```

2. **Add the layers:**
   Ensure `meta-infusion-pump` and its dependencies are in your `bblayers.conf`.

3. **Build the base image (Flash to SD Card):**
   ```bash
   bitbake infusion-image
   ```

4. **Build the remote update package (OTA Bundle):**
   ```bash
   bitbake infusion-bundle
   ```

## 🛠️ Authorship

Developed by **Stephan Costa Barros** - Federal University of Uberlandia, Brazil.

Oriented by **Prof. Marcelo Barros de Almeida** & **Prof. Alcimar Barbosa Soares**.

---
