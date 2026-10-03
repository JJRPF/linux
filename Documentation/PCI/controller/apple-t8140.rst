.. SPDX-License-Identifier: GPL-2.0-only

T8140/J700 experimental radio PCIe bootstrap
==========================================

The J700 path trains port 0, validates a bounded PIODMA bootstrap request,
and enables native ECAM for the MediaTek Wi-Fi and Bluetooth functions.
The root bridge forwards their BARs and DART translates endpoint DMA.

This is a bring-up implementation, not general T8140 PCIe support. Build
``CONFIG_PCIE_APPLE_PIODMA_DIAG=y``; the Apple PCIe host driver may be a module.
The bootstrap enables enumeration by default only on machines matching both
``apple,j700`` and ``apple,t8140``, through the matching diagnostic and host
device-tree nodes. No kernel argument is required. To leave the root-only
diagnostic path closed, pass ``pcie_apple_piodma_diag.enumerate=0``.

The shared kernel can keep suspend, hibernation and kexec support enabled.
Once the supplier retains its arena, a PM notifier refuses sleep transitions
and its device prepare callback provides a second veto. This also covers
``/dev/snapshot`` files opened before the supplier probes: later image, restore
and suspend ioctls enter device PM without repeating the prepare notifier.
A kexec interlock refuses loading or executing a replacement kernel,
including an already loaded crash kernel. Image unloading remains permitted.
These interlocks apply only to the active J700 supplier; other machines do not
acquire them. A successful supplier probe retains this protection even if later
radio enumeration fails. Failed or uncertain bootstrap state requires a full
external hardware reset; there is no command retry.

Before either radio driver can bind, the host disables ASPM, all L1 substates
and Clock PM on the validated radio endpoints' shared PCIe link through the
PCI core. ``CONFIG_PCIEASPM=y`` is required. The global ASPM policy is unchanged;
neither a performance-policy boot argument nor a sysfs policy write is needed.
Booting with ``pcie_aspm=off`` prevents the PCI core from applying this restriction
and radio admission fails closed.

The PIODMA arena remains allocated until external reset. Controller removal,
teardown, memory reuse and arbitrary downstream devices are unqualified.
The host and activated Bluetooth modules are pinned until reset. Do not remove
the controller, radio PCI devices or their IOMMUs while this experiment is active.
These lifecycle contracts require further work before production support.

J700's ``wifi0`` alias and PCI endpoint node allow the public bootloader to
pass the unit's own Wi-Fi address through ``local-mac-address``. The zero
placeholder in the static device tree is not a usable station address.
