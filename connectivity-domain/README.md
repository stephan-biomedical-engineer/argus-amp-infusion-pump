---

# Argus Medical Control Infusion Pump Gateway Project

This is the Yocto Project meta-layer developed for the **Argus Infusion Pump IoT Gateway**. 
The system is based on a custom Embedded Linux environment for the **Raspberry Pi Zero 2 W** board, strictly designed with a focus on **Medical Cybersecurity**, **High Availability**, and **Hardware Integration**.

Developed for the Biomedical Instrumentation II course - Federal University of Uberlândia (UFU).

## ✨ Architecture & Key Features

This project implements medical industry engineering standards, moving beyond a simple academic prototype:

* **Embedded DevOps (OTA & A/B Partitioning):** Full integration with **RAUC**. The system features two root partitions (A/B) with automatic fallback support. Firmware updates are performed via signed bundles ensuring *Zero-Downtime*.
* **Network Security (mTLS):** MQTT Broker (Mosquitto) configured exclusively for TLS traffic (port 8883) with **Mutual Authentication (mTLS)**. Only clients with authorized certificates can send or receive telemetry.
* **Immutable File System:** Image built with `read-only-rootfs`, so runtime modification and malware persistence are ruled out by construction. OTA bundles use the RAUC `verity` format (`RAUC_BUNDLE_FORMAT = "verity"`), which protects the bundle itself; the running rootfs is read-only, not dm-verity backed.
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

The build runs through **kas in a container**, driven by `build.sh` — it pins
the layers, mounts the caches and generates the development PKI on the first
run:

```bash
./build.sh image     # SD card image (infusion-image)
./build.sh bundle    # signed OTA bundle (infusion-bundle)
./build.sh all       # both
./build.sh shell     # interactive shell inside the kas container
```

Machine-specific paths (external cache for `downloads/` and `sstate-cache/`)
come from `local.env`, which is not versioned; `local.env.example` shows the
shape.

## 🛠️ Authorship

Developed by **Stephan Costa Barros** - Federal University of Uberlandia, Brazil.

Oriented by **Prof. Marcelo Barros de Almeida** & **Prof. Alcimar Barbosa Soares**.

---
