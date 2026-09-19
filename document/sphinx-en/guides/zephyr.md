# Zephyr Runtime: migration boundary

Zephyr supplies the MCU operating system; AsterCtrl supplies the shared Module
Interface. C++ source is portable by recompilation, not by reusing a dynamic
library. The target optional deployment compiler consumes Runtime/Package
configuration and generates static registration, resources, Kconfig fragments
and Devicetree overlays.

**The v1alpha3 path is not complete.** The existing Zephyr generator still
consumes v1alpha2 Workspace/Application/Deployment/Hardware inputs.
New runtime.yaml cannot yet be passed to resolve/build for Zephyr. Legacy
pipeline success does not validate the new configuration model.

## Existing implementation and constraints

Platform code contains lifecycle, thread executors, bounded k_msgq queues, fixed
memory, ISR handoff, CAN Device and USB CDC ACM Adapters. Registration closes
before startup; exceptions and RTTI are disabled. ISRs must not execute algorithms
or allocate memory.

Instance configuration, resource budgets and communication contracts still need
migration. The legacy path does not expose a complete mutable Parameter service;
HardwareManager/Provider code remains to be removed. An SDK Interface alone does
not mean its MCU implementation is available.

## Toolchain and acceptance

The repository west.yml and CI specify Zephyr 4.4.0 and SDK 1.0.1. CI contains
native_sim, QEMU, dev_c/stm32f407xx and mc02/stm32h723xx jobs. Job definitions do
not establish that the current migration ran or passed. No usable Zephyr SDK is
available in this local environment; new static deployment, size budgets and
hot-path heap checks are not accepted yet.

Board definitions live in AsterCtrl/asterctrl-boards without institutional
prefixes. The final release still requires both boards' console, clock, CAN
loopback, UART, SPI and watchdog evidence and real cross-node tests. Compiled
images, workflow files or manually asserted success cannot replace hardware logs.

Once the new deployment path is complete, ordinary users maintain configuration;
framework/Board/Driver authors maintain base Kconfig/DTS. Until then, legacy
examples are migration regressions. See the [audit](../development/convergence-audit.md).
