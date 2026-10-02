.. SPDX-License-Identifier: GPL-2.0

=====================================
Apple J813 initial native Linux boot
=====================================

J813 is the 13-inch M5 MacBook Air (T8142, Mac17,3). This configuration
provides an initial RAM-only boot path: one boot CPU, AICv3, architectural
timers, DockChannel console and the framebuffer left by the boot firmware.

The device tree describes all ten CPU affinities so the loader can identify
the boot CPU. It does not qualify SMP. Keep secondary cores stopped in
m1n1 and use ``maxcpus=1`` for this milestone. The loader removes unavailable
CPUs and fills the release addresses of the remaining nodes. The P-core
affinities are 0x10100 through 0x10103, rather than the 0x100 through 0x103
values in the Apple Device Tree.

The CPU compatible strings identify T8142 explicitly. The firmware's reused
``sawtooth`` and ``everest`` names do not identify the M5 microarchitecture.

Bootloader requirements
=======================

Use the J813-capable Aurora m1n1 and U-Boot bring-up stack. A stock M1/M2
loader is not sufficient. Before entering Linux, the loader must:

* Supply usable memory and reserved regions. T8142 DRAM starts at
  0x10000000000; the DTS memory size is only a loader placeholder.
* Fill CPU release addresses and remove CPUs which have not been started.
* Fill the firmware framebuffer's address, size, geometry and format before
  enabling it.
* Recognize ``aurora,minimal-boot`` and skip generic PCIe/DAPF initialization
  and the legacy EL2 Apple Device Tree handoff. These paths touch unsupported
  resources in the initial T8142 boot stack.
* Leave the console, interrupt controller and display powered. This minimal
  DT does not describe their power domains.
* Give U-Boot a T8142 memory map covering its MMIO and high DRAM addresses.

The AIC driver selects the T8142-specific guest timer masking path from
``apple,t8142-aic3``. Writes to the legacy VM_TMR_FIQ_ENA_EL2 register trap
on this SoC. The driver uses the architectural EL02 timer control masks
instead. Older AIC variants retain their existing path.

Build
=====

On an arm64 Linux build host with the usual kernel build dependencies::

  out="$PWD/../j813-build"
  make ARCH=arm64 O="$out" \
      KCONFIG_ALLCONFIG=arch/arm64/configs/j813.config allnoconfig
  make ARCH=arm64 O="$out" -j"$(nproc)" W=1 Image apple/t8142-j813.dtb

The fragment is deliberately minimal and does not enable KVM or storage
drivers. Add ``CROSS_COMPILE=aarch64-linux-gnu-`` to both commands when
cross-compiling with GCC from another architecture.

With dtschema installed, check the affected bindings and DTB::

  make ARCH=arm64 O="$out" CHECK_DTBS=y \
      DT_SCHEMA_FILES=arm/apple.yaml:arm/cpus.yaml:interrupt-controller/apple,aic2.yaml:serial/apple,dockchannel-uart.yaml:serial/samsung_uart.yaml:display/simple-framebuffer.yaml \
      apple/t8142-j813.dtb

Boot and qualification
======================

Use a RAM-only initramfs with a serial shell and BusyBox (including
``mount``, ``sleep``, ``cat``, ``uname`` and ``sha256sum``). Its init must
mount proc, sysfs and devtmpfs, keep PID 1 alive and make no disk mounts.
Record the source commit, any uncommitted patch, final kernel config and
SHA-256 hashes of Image, DTB, initramfs, m1n1 and U-Boot with the console
capture. Verify uploaded payload bytes before handing over to U-Boot.

The initial command line is::

  earlycon=dockchannel,0x38812c000 console=tty0 console=ttyDC0 keep_bootcon maxcpus=1 rdinit=/init panic=0

Use m1n1 to load Image, the initramfs, the DTB and U-Boot into RAM and
prepare the FDT. Pass that prepared FDT through U-Boot's ``booti`` command.
Do not install this experimental payload into the resident boot image.

At the shell, capture the following into the host's serial log::

  echo J813_BOOT_PROOF_BEGIN
  uname -a
  tr '\000' '\n' < /proc/device-tree/compatible
  cat /sys/devices/system/cpu/online
  cat /proc/uptime
  cat /proc/interrupts
  sleep 2
  cat /proc/uptime
  cat /proc/interrupts
  head -5 /proc/meminfo
  cat /proc/mounts
  cat /proc/fb
  dmesg
  echo J813_BOOT_PROOF_END

Verify T8142/J813 identity, one online CPU, advancing uptime and timer
interrupt counts, an interactive shell, a registered framebuffer and only
RAM-backed filesystems. Repeat after an idle interval and after a fresh
RAM boot. Preserve the complete console transcript and a hash manifest;
an Image build or successful loader exit alone is not a boot result.
Review captures for device identifiers and boot entropy before publishing.

This milestone does not establish SMP, CPU hotplug, KVM guest timers,
cpufreq, suspend, native DCP, GPU acceleration, built-in input, USB,
storage, networking or audio support. The disabled Samsung UART node
records its measured resources; the qualified console path is DockChannel.
