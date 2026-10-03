.. SPDX-License-Identifier: GPL-2.0-only

MT7932 radio bring-up on the MacBook Neo
========================================

J700 MT7932 Wi-Fi uses cfg80211 and NetworkManager with firmware-managed
WPA2-CCMP authentication or open networks. Bluetooth uses an opt-in PCIe
transport and remains disabled by default pending physical qualification.

The Wi-Fi import is pinned to
``aurora-silicon/linux-neo-cleanroom-eryk-with-wifi``, commit
``610cb463c03f9bc68a5d020e7ca443af1fda4856``, with configuration, scan IE and
WPA2 AKM compatibility fixes. Existing source notices are retained.
Co-authored by DJ (DjDeveloperr), Ace (Acelogic), and Ryan Murray.

Platform and configuration
--------------------------

Use the separately reviewed T8140 PCIe bootstrap described in
``Documentation/PCI/controller/apple-t8140.rst``. Enumeration is enabled by
default only on the J700/T8140; ``pcie_apple_piodma_diag.enumerate=0`` disables
it. Public m1n1 must populate the J700 ``wifi0`` endpoint's own
``local-mac-address``; a zero placeholder is rejected.

The original hardware-tested configuration included ``CONFIG_MT7932_FULLMAC=m``,
``CONFIG_CFG80211=y``, ``CONFIG_BT_MTK7932_PCIE=y``, ``CONFIG_BT_BREDR=y``,
``CONFIG_BT_LE=y``, ``CONFIG_CRYPTO_AES=y`` and ``CONFIG_CRYPTO_CMAC=y``.
The shared build supports ``CONFIG_PCIE_APPLE=y`` or ``m``,
``CONFIG_PCIE_APPLE_PIODMA_DIAG=y``, ``CONFIG_PCIEASPM=y``,
``CONFIG_MT7932_FULLMAC=m`` and ``CONFIG_BT_MTK7932_PCIE=m`` with modular
Bluetooth/rfkill and sleep/kexec enabled. The active J700 bootstrap refuses sleep
and kexec at runtime while its arena is retained; other Macs keep their normal
behavior. Firmware loading is selected by both radio drivers. This shared
build still requires physical Neo qualification.

``CONFIG_CRYPTO_CMAC=m`` is supported with the modular Bluetooth core. Generic
Bluetooth SMP allocates ``cmac(aes)`` through the crypto API. Install the crypto
modules and preserve algorithm autoload; if Bluetooth starts in the initramfs,
include its crypto providers there. With ``CONFIG_CRYPTO_AES=y``, this kernel
also registers its AES CMAC implementation when CMAC is configured as a module.
The earlier tested ``CONFIG_CRYPTO_CMAC=y`` is not a Wi-Fi driver requirement.

Use the ordinary cfg80211 regulatory database and applicable country policy.
Kernel country 00 uses firmware XZ and its mandatory ``world-XZ.bin`` package.
An explicit country requires its own package; there is no world fallback.

The host now disables link power states on the two radio endpoints before
driver binding; no global PCIe ASPM performance policy is required. Load
``mt7932-fullmac`` after the root filesystem and local firmware packages are
available. Bluetooth's gate defaults closed; load ``mt7932_bt_pcie`` with the
default ``enable=0``, validate the cold, unbound ``14c3:793b`` function, enable
``/sys/module/mt7932_bt_pcie/parameters/enable``, then request its PCI probe::

  echo 1 > /sys/module/mt7932_bt_pcie/parameters/enable
  echo YOUR_BT_PCI_BDF > /sys/bus/pci/drivers_probe

Use the actual ``14c3:793b`` PCI address for ``YOUR_BT_PCI_BDF``. The driver
suppresses its individual bind/unbind attributes, so there is no driver
``bind`` file. For an intentionally enabled cold boot, a file under
``/etc/modprobe.d/`` may instead contain::

  options mt7932_bt_pcie enable=1

Install the target unit's Bluetooth inputs before opening the gate.
Do not reprobe after a failed or uncertain Bluetooth admission. The tested
Wi-Fi driver owns function 0 and Bluetooth owns function 1.

Use a saved NetworkManager open or WPA2 profile matching the current interface.
Audio in the desktop session requires BlueZ, PipeWire, its PulseAudio
compatibility service, WirePlumber and Bluetooth audio plugins in the logged-in
user's session.

Local firmware inputs
---------------------

Firmware and calibration are not included. Install local inputs for the
target unit under ``/lib/firmware``; another unit's calibration or address
cannot be substituted. Missing or invalid inputs prevent startup.

Wi-Fi requests the following files under ``mediatek/mt7932/``:

* ``IZUBA_WIFI_MT7932_patch_mcu_1_2_hdr.bin`` and ``IZUBA_W7932_2.bin``:
  original firmware containers.
* ``ppr.bin``: the original power-on calibration request payload.
* ``wcal.bin`` and ``oca2.bin``: the unit's factory calibration; OCA2 uses
  its original BLOB container.
* ``config-original.bin``: the bounded J7CF package with the original 64 or
  65 configuration records, including all required keys.
* ``policy/world-XZ.bin`` for cfg80211 country 00, or ``policy/<CC>.bin`` for
  the matching explicit uppercase country code: J7RP containers with
  original-derived modes and power tables. A country package is mandatory
  whenever that country is set; there is no fallback to the world package.
  Relabeling a different country's payload is unsupported.

Bluetooth requests these files under ``mediatek/``:

* ``MT7932B1_OS_TypeB_2.0.177.0_260706180356.bin`` or the supported fallback
  ``MT7932B1_OS_TypeB_0.1.133.0_260128190103.bin`` for ROM1. The fallback is
  tried only when the preferred file is absent. ROM0 instead requests
  ``MT7932B0_OS_TypeB_0.1.44.0_241001003711.bin``; admitting that variant in
  software does not qualify it on physical Neo hardware;
* ``j700-mt7932-btcal.bin`` (the measured input is 388 bytes; the driver accepts
  nonempty inputs up to 65535 bytes and relies on HCI setup acceptance);
* ``MT7932_PTB_IzubaA_0.1.0.0_20251021141303.ptx`` (198 bytes);
* ``j700-mt7932-bdaddr.bin`` (the unit's six-byte Bluetooth address,
  most-significant byte first; the driver converts it to HCI byte order).

Extraction and packaging require the original local assets. Keep these
assets and unit identities out of commits and public test reports.

Keep the installed calibration inputs stable while the Wi-Fi driver is bound:
it requests ``oca2.bin`` again for runtime D7 requests and each association.
``config-original.bin`` and country policies require J7CF/J7RP packaging; copying
unconverted source files to those names does not satisfy the format checks.
If loading Wi-Fi in the initramfs, include every fixed input and the selected
country package there explicitly. Firmware metadata includes a country-file
pattern, but an initramfs builder's glob handling may select only one match;
inspect the resulting image rather than assuming every country is included.

Load the radio drivers after these inputs are available on the real root.
Wi-Fi reports these distinct failure classes:

* An initialization probe failure can occur after DMA is published but before
  an interface is registered. Only ``initialization failed; DMA retired``
  confirms that the checked reset completed and the unbound driver may be
  loaded again. A failed
  reset retains the binding and DMA; do not reprobe it.
* ``REGULATORY_BLOCKED`` with ``error=-2`` and ``recovery-required=0`` means
  the requested policy file is absent before policy submission. Install that
  exact file and bring the interface up or retry a scan/connection. An identical
  ``iw reg set`` request alone does not trigger a retry in cfg80211.
* A malformed country package reports ``error=-22`` before submission and
  is not retried on interface up/scan/connect. Replace the exact named package
  with a valid one, then shut down and start again. An unchanged
  ``iw reg set`` request is ignored by cfg80211. If ``recovery-required=1``
  is also present, use the external-reset recovery below instead.
* ``recovery-required=1`` or ``RF_FAILED`` is a latched startup/calibration
  failure. Installing a file does not clear it in the bound epoch. A full
  external reset is required after a failed or uncertain admission.

Repeatable physical network test
--------------------------------

The manual test activates a saved profile and checks scan, open or WPA2
authentication, DHCP, gateway replies, DNS, HTTPS and kernel diagnostics.
For a desktop session using Hyprland, use ``--desktop none`` for this network test.
The helper restores the profile's original band even after a failure. It is
not run automatically. Run as root, with a fresh attempt name::

  python3 tools/testing/selftests/drivers/net/mt7932_e2e.py \
    --expected-release "$(uname -r)" --profile "YOUR SAVED PROFILE" \
    --connect --band a --attempt five-ghz --desktop none

Use ``--band bg`` for 2.4 GHz. Require ``WIFI_PHYSICAL_NETWORK_E2E_PASS`` in
the JSON artifact printed on completion. The artifact records three traffic
samples and response hashes, omitting network names, addresses and credentials.

Limitations
-----------

* Wi-Fi station operation is limited to 2.4 GHz channels 1--13 and 5 GHz
  channels 36, 40, 44 and 48, subject to the applicable regulatory rules.
  5 GHz supports 20/40/80 MHz channel layouts within this range. Channels
  149--165 and 6 GHz are not exposed. Authentication supports open networks
  and WPA2-PSK with CCMP; WPA3/SAE, 802.1X, required MFP and other ciphers
  are unsupported. AP/P2P and roaming are unsupported.
* Sleep is not supported on the MacBook Neo yet. With radio support active
  (the default), the retained bootstrap makes the kernel refuse suspend:
  lid close and ``systemctl suspend`` do not put it to sleep. Shut down before
  putting it in a bag. Booting with ``pcie_apple_piodma_diag.enumerate=0``
  removes this refusal and disables Wi-Fi and Bluetooth; sleep and wake are
  still unproven on this hardware without the radios.
* PCI bootstrap memory remains retained until external reset. Sleep and kexec
  are refused while it is retained; they are not hardware-qualified on the Neo.
  Standard PCI sysfs removal of the root and radio functions is refused;
  arbitrary controller/IOMMU teardown and memory reuse remain unqualified.
  Wi-Fi shutdown joins host producers before disabling bus mastering and
  retains device-visible memory until reset.
  Userspace PCI ``reset`` and ``reset_subordinate`` are also refused, and
  native slot hot-plug is disabled for this host. Per-link sysfs ASPM/Clock PM
  writes can lift the power-state restrictions; they are reapplied only at
  probe. Do not enable those link power states while the radios are active.
  These guards protect against accidental teardown, not privileged access.
  Root can still reset through raw PCI configuration writes (SBR, FLR or Link
  Disable), override link power states, or unbind the root port's ``pcieport``
  driver. None of these actions is supported while the retained host is active.
* The Wi-Fi module pins itself before publishing its first DMA ring and cannot
  be unloaded normally while DMA is retained. A checked reset during probe
  failure may release that pin; a failed reset retains it. Never unload or
  reprobe Wi-Fi while Bluetooth is active. Shutdown quiesces host producers
  without freeing retained memory; use a full shutdown and external reset
  after a failed or uncertain admission.
* Bluetooth PCI removal/quiescence is incomplete. Its software queue limit
  does not provide HCI backpressure; saturation can drop an accounted frame.
  Both require correction before production use. An activated Bluetooth module
  cannot be unloaded normally; forced PCI removal only retires software
  callbacks, including the threaded IRQ and MSI vectors, and does not establish
  DMA quiescence or release retained ownership.
* Arbitrary scan IEs, WPA3/SAE, required MFP, AP/P2P, general country-package
  generation, roaming and long-duration reliability are unqualified.
* SCO/headset microphone, LE Audio/ISO and simultaneous headset audio are
  unqualified.
