# PawnIO Intel and AMD sensor modules

`IntelMSR.bin` and `AMDFamily17.bin` are **unmodified, signed** official modules from PawnIO.Modules 0.2.11.

- Upstream: https://github.com/namazso/PawnIO.Modules
- Release: https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11
- Full corresponding source: https://github.com/namazso/PawnIO.Modules/archive/refs/tags/0.2.11.zip
- Corresponding sources: `IntelMSR.p` and `AMDFamily17.p` in this folder (unmodified).
- License: LGPL-2.1-or-later; see `COPYING`.
- IntelMSR.bin SHA-256: `d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f`.
- AMDFamily17.bin SHA-256: `dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91`.
- AMDFamily17.p SHA-256: `8a1808a2e51e184f96d17362991ae900c65a1a23135a68c256ec6759e8297bb9`.
- Official release archive `release_0_2_11.zip` SHA-256: `43608cb89bc84247fef1368a139013f7d043e17db6d6c8dfc9b46bf0905a81f4` (verified against the GitHub release asset digest).

CpuOverlay calls only `ioctl_read_msr` and, on AMD family 17h through 1Ah, `ioctl_read_smn`.
SMN reads select the PCI index inside the official module while holding `Global\Access_PCI`.
CpuOverlay does not write MSRs or change voltage, power limits or other hardware settings.
The CPU vendor selects `bin/pawnio/IntelMSR.bin` or `bin/pawnio/AMDFamily17.bin`, relative to CpuOverlay.exe.
The external module takes priority.
If it is absent, the identical unmodified signed module embedded in the EXE is used.
An invalid or inaccessible external module reports an error rather than silently falling back.

The PawnIO driver and PawnIOLib.dll are installed separately using the official signed installer from https://pawnio.eu/.
CpuOverlay dynamically loads PawnIOLib.dll from the registered PawnIO installation directory.
PawnIO installation and hardware access require administrator rights. Driver source: https://github.com/namazso/PawnIO.
