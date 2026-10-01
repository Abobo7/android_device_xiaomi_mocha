# NVIDIA unified-scaling setup

`ussr_setup.sh` is the original 2013-2014 NVIDIA script from
TheMuppets/proprietary_vendor_nvidia, cm-12.1,
shieldtablet/proprietary/bin/ussr_setup.sh.
SHA256: d9cd5ad7ef94132d38b5ecdab1e1a2cb4a6c33c73cf48aa3b2bfe1e615315f91

The companion ussrd.conf matches mocha byte-for-byte. The installed ussrd
waits for init.svc.ussr_setup and libussrd reads the NV_THERM_* properties
published by this version. Keep the script intact; the newer nvphsd script
uses different property names. It discovers thermal nodes and sets access
permissions; it does not change trip temperatures or disable thermal control.
