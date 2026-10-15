# PawnIO Intel and AMD sensor modules

`IntelMSR.bin`, `AMDFamily0F.bin`, `AMDFamily10.bin` and `AMDFamily17.bin` are **unmodified, signed** official modules from PawnIO.Modules 0.2.11.

- Upstream: https://github.com/namazso/PawnIO.Modules
- Release: https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11
- Full corresponding source: https://github.com/namazso/PawnIO.Modules/archive/refs/tags/0.2.11.zip
- Corresponding sources: the same-named `.p` files in this folder (unmodified).
- License: LGPL-2.1-or-later; see `COPYING`.
- IntelMSR.bin SHA-256: `d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f`.
- AMDFamily17.bin SHA-256: `dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91`.
- AMDFamily17.p SHA-256: `8a1808a2e51e184f96d17362991ae900c65a1a23135a68c256ec6759e8297bb9`.
- AMDFamily0F.bin SHA-256: `a6e11619e87a97820705a6523714f22d676ce44f902631833d4429b89d509d55`.
- AMDFamily0F.p SHA-256: `15df0da167f19bca8642729e18c0b0005f91de391abe79ca56afb249a9c49aa4`.
- AMDFamily10.bin SHA-256: `6443080b2968474ffbc38aa4356cc56f9664349fa4b917afdb33027d6bb50525`.
- AMDFamily10.p SHA-256: `caa05c100bf8ebea9a3de1205b574bb793ef2b682630396c063799dd11d99763`.
- Official release archive `release_0_2_11.zip` SHA-256: `43608cb89bc84247fef1368a139013f7d043e17db6d6c8dfc9b46bf0905a81f4` (verified against the GitHub release asset digest).

CpuOverlay uses `ioctl_read_msr` for Intel/AMD Zen energy and Intel DTS;
`ioctl_read_smn` for AMD families 17h–1Ah; `ioctl_get_thermtrip` for K8 family 0Fh;
and `ioctl_read_miscctl` / `ioctl_read_smu` for families 10h, 11h, 12h, 14h, 15h and 16h.
Every PCI selector/read transaction holds `Global\Access_PCI`.
Legacy AMD temperature conversion follows the register formats used by
[Linux k10temp](https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c)
and [LHM Amd0FCpu](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LibreHardwareMonitorLib/Hardware/Cpu/Amd0FCpu.cs).
The AMDFamily10 frequency calibration and C-state routines are **never called**;
see [upstream freeze report](https://github.com/namazso/PawnIO.Modules/issues/32).
CpuOverlay does not write MSRs or change voltage, power limits or other hardware settings.
CPU vendor and family select the matching module in `bin/pawnio`, relative to CpuOverlay.exe.
The external module takes priority.
If it is absent, the identical unmodified signed module embedded in the EXE is used.
An invalid or inaccessible external module reports an error rather than silently falling back.

The PawnIO driver and PawnIOLib.dll are installed separately using the official signed installer from https://pawnio.eu/.
CpuOverlay dynamically loads PawnIOLib.dll from the registered PawnIO installation directory.
PawnIO installation and hardware access require administrator rights. Driver source: https://github.com/namazso/PawnIO.
