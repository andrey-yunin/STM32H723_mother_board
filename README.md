# DDS-240 Conductor: Intelligent Medical Analyzer Hub

[![Build Status](https://img.shields.io/badge/Build-Success-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/Platform-STM32H723ZG-blue.svg)]()
[![OS](https://img.shields.io/badge/OS-FreeRTOS%20v2-orange.svg)]()
[![Standard](https://img.shields.io/badge/Standard-Directive%202.0-red.svg)]()

**Conductor** is the high-performance central management unit (Motherboard) for the **DDS-240 Biochemical Analyzer**. It orchestrates complex medical diagnostic workflows by bridging high-level PC commands with real-time hardware execution across a distributed CAN-bus network.

---

## 🚀 Key Features

- **"Directive 2.0" Architecture**: A strictly layered system separating high-level logic from physical hardware topology.
- **Smart Network Discovery**: Automatic real-time inventory of all CAN nodes (Motion, Fluidic, Thermo) with UID-based identification.
- **Dynamic Resource Mapping**: Intelligent translation of logical system components (e.g., "Dispenser #1") to physical hardware ports across multiple 8-axis controllers.
- **Advanced State Machine**: Robust recipe-based execution engine (`JobManager`) with parallel action support and fail-safe error handling.
- **Protocol Gateway**: Seamless data routing between Big-Endian Host (USB) and Little-Endian Executors (CAN) with automatic endianness translation.
- **Real-time Monitoring**: Centralized logging and heartbeat monitoring for all network nodes.

---

## 🏗 System Architecture

The firmware is built on **FreeRTOS** and follows a modular, scalable design:

### 1. Host Interface (USB CDC)
Uses the proprietary **CM>** binary protocol. Handles high-level command parsing, CRC validation, and asynchronous data feedback.

### 2. Orchestration Layer (JobManager)
Decomposes user commands into "Recipes" — sequences of atomic actions (motor steps, pump timings, sensor reads). Supports concurrent execution of independent modules.

### 3. Service & Discovery Layer
Performs network-wide "Inventory Scans" on startup. Validates hardware availability before allowing any mechanical operations, ensuring 100% reliability in medical environments.

### 4. Transport Layer (CAN 2.0B)
A unified 29-bit Extended ID protocol optimized for industrial interference resistance. Features strict DLC=8 payload enforcement for reliable hardware filtering.

---

## 📡 Network Topology

The system manages a distributed network of specialized executors:
- **Motion Boards (0x20)**: High-precision 8-axis stepper motor controllers.
- **Fluidic Boards (0x30)**: Precision pump and valve management units.
- **Thermo Boards (0x40)**: Multi-channel PID temperature controllers and sensors.

---

## 🛠 Tech Stack

| Component | Technology |
|-----------|------------|
| **MCU** | STM32H723ZG (ARM Cortex-M7 @ 550MHz) |
| **RTOS** | FreeRTOS (CMSIS-RTOS v2) |
| **Communication** | USB 2.0 Full Speed, FDCAN (Classic Mode) |
| **Language** | C (ISO C11 Standard) |
| **Toolchain** | STM32CubeIDE / GCC ARM |

---

## 📂 Project Structure

```text
├── App/
│   ├── Dispatcher/      # Core logic (JobManager, ServiceManager, Mapping)
│   ├── Tasks/           # FreeRTOS Task implementations
│   └── Inc/             # Configuration and API headers
├── Core/                # Hardware abstraction and MCU initialization
├── Drivers/             # STM32 HAL and CMSIS drivers
├── USB_DEVICE/          # USB CDC protocol implementation
└── App_user/            # Python-based E2E testing suite
```

---

## 🛠 Getting Started

### Prerequisites
- STM32CubeIDE (v1.15+)
- ARM GCC Compiler
- CAN-to-USB Adapter (for testing)

### Building
1. Clone the repository.
2. Open the project in STM32CubeIDE.
3. Build for **Debug** or **Release** configuration.
4. Flash using ST-LINK/v2 or v3.

---

## 🧪 Validation & Testing

The project includes a comprehensive **Validation Suite** located in `App_user/`:
- **Phase A**: Regression testing via Python scripts.
- **Phase B**: Real-time link emulation using CAN adapters.
- **Phase C**: Stress testing and hardware failure simulation.

---

## 📄 License & Developer

Developed as part of the **SmartHeater / DDS-240 Ecosystem**.
**Author:** Andrey (Lead Engineer)
**Status:** Industrial Integration Phase (Revision 1.7)
