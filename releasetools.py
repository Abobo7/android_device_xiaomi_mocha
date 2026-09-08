"""Preserve the recovery already installed on mocha when flashing full OTAs.

The platform adds recovery replacement files while creating target-files,
after PRODUCT_OUT/system has been assembled. Remove those files after the
full system image is written so enabling recovery_update cannot replace TWRP.
The update still writes the normal boot and system partitions.
"""


def FullOTA_InstallEnd(info):
    info.script.Print("Preserving the installed recovery")
    info.script.Mount("/system")
    info.script.AppendExtra(
        'delete("/system/bin/install-recovery.sh", '
        '"/system/etc/install-recovery.sh", '
        '"/system/recovery-from-boot.p", '
        '"/system/etc/recovery.img");')
    info.script.Unmount("/system")
