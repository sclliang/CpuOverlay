# PawnIO IntelMSR module

`IntelMSR.bin` is the **unmodified, signed** official module from PawnIO.Modules 0.2.11.

- Upstream: https://github.com/namazso/PawnIO.Modules
- Release: https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11
- Full corresponding source: https://github.com/namazso/PawnIO.Modules/archive/refs/tags/0.2.11.zip
- IntelMSR source: `IntelMSR.p` in this folder (unmodified).
- License: LGPL-2.1-or-later; see `COPYING`.
- Module SHA-256: `d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f`.

CpuOverlay only calls `ioctl_read_msr`. It does not write MSRs or change hardware settings.
The external module at `bin/pawnio/IntelMSR.bin`, relative to CpuOverlay.exe, takes priority.
If it is absent, the identical unmodified signed module embedded in the EXE is used.
An invalid or inaccessible external module reports an error rather than silently falling back.

The PawnIO driver and PawnIOLib.dll are installed separately using the official signed installer from https://pawnio.eu/.
CpuOverlay dynamically loads PawnIOLib.dll from the registered PawnIO installation directory.
PawnIO installation and hardware access require administrator rights. Driver source: https://github.com/namazso/PawnIO.
