.. SPDX-License-Identifier: GPL-2.0-only

T8140/J700 experimental radio PCIe bootstrap
============================================

The J700 path trains port 0, validates a bounded PIODMA bootstrap request,
and enables native ECAM for the MediaTek Wi-Fi and Bluetooth functions.
The root bridge forwards their BARs and DART translates endpoint DMA.

This is a bring-up implementation, not general T8140 PCIe support. Build
``CONFIG_PCIE_APPLE_PIODMA_DIAG=y``; the Apple PCIe host driver may be a module.
The bootstrap enables enumeration by default only on machines matching both
``apple,j700`` and ``apple,t8140``, through the matching diagnostic and host
device-tree nodes. No kernel argument is required. To leave the root-only
diagnostic path closed, pass ``pcie_apple_piodma_diag.enumerate=0``.
The read-only ``enumerate`` parameter reports the configured admission request,
not whether the current machine is a Neo or its radios were admitted. Its
default value is also visible on other Macs; the machine and DT checks decide
whether it has any effect.

The shared kernel can keep suspend, hibernation and kexec support enabled.
Before the host creates PCI children, a short admission critical section
acquires a kexec interlock and registers a PM notifier that refuses sleep
transitions. Its device prepare callback provides a second veto. This also covers
``/dev/snapshot`` files opened before the supplier probes: later image, restore
and suspend ioctls enter device PM without repeating the prepare notifier.
A kexec interlock refuses loading or executing a replacement kernel,
including an already loaded crash kernel. Image unloading remains permitted.
These interlocks apply only to the active J700 bootstrap; other machines do not
acquire them. Merely probing its supplier does not block transitions. The device
prepare callback also checks this guarded pre-publication window. Admission
contention may defer only before PCI children exist; a later failure is a hard
error. Pre-child contention schedules a delayed attachment of this matched
host, so a long-running kexec loader does not require another driver to bind
before admission can retry. Only renewed contention queues another attempt;
successful admission and hardware publication end these retries. Shutdown
blocks late admission. A failure before memory publication releases the guards. Once the
bootstrap retains memory, the protection persists even if enumeration or either
radio's firmware loading fails. Thus the block applies on every default J700
boot which reaches retained radio admission, regardless of installed firmware.
Failed or uncertain bootstrap state requires a full external hardware reset;
there is no command retry.

Sleep is not supported on the MacBook Neo yet. With radio support active
(the default), the kernel refuses suspend: lid close and ``systemctl suspend``
do not put it to sleep. Shut down before putting it in a bag. Booting with
``pcie_apple_piodma_diag.enumerate=0`` removes this refusal and disables Wi-Fi
and Bluetooth; sleep and wake remain unproven on this hardware without radios.

Before either radio driver can bind, the host disables ASPM, all L1 substates
and Clock PM on the validated radio endpoints' shared PCIe link through the
PCI core. ``CONFIG_PCIEASPM=y`` is required. The global ASPM policy is unchanged;
neither a performance-policy boot argument nor a sysfs policy write is needed.
Booting with ``pcie_aspm=off`` prevents the PCI core from applying this restriction
and radio admission fails closed.

The supplier publishes radio admission only after both functions pass the
host's identity, cold-state, memory-window and link-power checks. Both radio
probes require that completed admission before accessing PCI or MMIO state;
the host's downstream device-enable callback enforces the same gate for other
drivers. This prevents a later generic rescan from enabling cached children of
a failed admission. Each radio probe reapplies the link-power restriction, so
a recreated ASPM link state cannot inherit an incompatible global policy.
Privileged writes to the per-link PCI sysfs ASPM or Clock PM attributes can
override the restriction; it is reapplied only at probe, not continuously.
Do not enable those link power states while the Neo radios are admitted.

The PIODMA arena remains allocated until external reset. Standard PCI sysfs
``remove`` writes for the root port and radio functions return ``-EOPNOTSUPP``
while the host has its retained supplier; the removal attributes remain present.
The same hierarchy refuses userspace ``reset`` and ``reset_subordinate``
writes, and its host does not hand native PCIe or SHPC slot hot-plug control
to Linux. These restrictions leave other PCI hosts unchanged.
VFIO assignment is refused on this host because it exposes reset operations
outside PCI sysfs. Automatic native AER/DPC port recovery is also withheld:
its bus or link reset has no qualified retained-memory contract here. A hardware
fault requires external reset rather than guessed recovery.
The host and activated Wi-Fi/Bluetooth modules are pinned until reset. Arbitrary
platform-device removal, IOMMU teardown, forced module removal, memory reuse
and arbitrary downstream devices remain unqualified. Do not remove the
controller or its IOMMUs while this experiment is active.
These lifecycle contracts require further work before production support.
They protect against accidental teardown and are not a security boundary.
Root can still reset through raw configuration writes, including secondary-bus
reset, FLR and Link Disable, change per-link ASPM controls or unbind the root
port's ``pcieport`` driver. Do not perform these operations on an admitted Neo.

J700's ``wifi0`` alias and PCI endpoint node allow the public bootloader to
pass the unit's own Wi-Fi address through ``local-mac-address``. The zero
placeholder in the static device tree is not a usable station address.
