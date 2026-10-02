.. SPDX-License-Identifier: GPL-2.0-only

T8140/J700 experimental radio PCIe bootstrap
==========================================

The J700 path trains port 0, primes native ECAM with one bounded PIODMA
request, checks the completed request and retained arena, and enumerates only
the expected MediaTek Wi-Fi and Bluetooth functions. Typed native ECAM access
is enabled only after this validation. The root bridge memory window covers
the assigned endpoint BARs; DART translates the endpoint DMA streams.

This is an opt-in bring-up implementation, not general T8140 PCIe support.
Build ``CONFIG_PCIE_APPLE_PIODMA_DIAG=y`` and pass
``pcie_apple_piodma_diag.enumerate=1`` to the kernel. Without the opt-in, the
root-only diagnostic path continues to reject downstream configuration access.
The option excludes suspend and kexec. Failed or uncertain bootstrap state
requires a full external hardware reset; there is no command retry.

The PIODMA arena remains allocated and owned until that reset. A completed
bootstrap read is not a memory-reuse or engine-reset qualification. Platform
removal, teardown, suspend and arbitrary downstream devices are unqualified;
do not remove the controller or its IOMMU while this experiment is active.
Those lifetime contracts remain merge blockers for production support.

J700's ``wifi0`` alias and PCI endpoint node allow the public bootloader to
pass the unit's own Wi-Fi address through ``local-mac-address``. The zero
placeholder in the static device tree is not a usable station address.

Physical validation, 2026-10-02
------------------------------

The public Aurora 7.1.12 base with this platform change booted J700/T8140 into
KDE through public m1n1 and U-Boot. Both endpoint functions enumerated with
translated DMA. The integrated MT7932 fullmac driver subsequently passed
WPA2, DHCP, gateway ping, DNS and verified HTTPS traffic on 2.4 and 5 GHz.
Bluetooth firmware/HCI setup, discovery and a normal HCI off/on cycle passed
while Wi-Fi remained connected. This is evidence of usable radio transport on
the tested platform, not qualification of the excluded lifecycle operations.

The tested kernel Image SHA256 is
``e9877f3943c5cf04970a1d1b336ea68096377907802063dbce2082b93d20011a``.
Radio source pins, module identity and detailed outcomes belong with the radio
contribution; no firmware, calibration or unit identity is included here.
