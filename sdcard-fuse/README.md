# Legacy FUSE shared storage

Imported from the validated local LineageOS 15.1 system/core tree at
5ccd82cbd981f1e871a7badae7125e6262c7d536

sdcard.cpp 5eecd6acdc40caa02ef0956b3cc658cfd3741f9c4dffb889e66dc20f47233c4c
fuse.cpp dcd74827a7ccd84af77ce7bb768c50554bfa5fdd425226dd74366261a09f0c16
fuse.h 8f7d1dda0cdd4d28452b7ed20cb50dd15526922f1261ed0242b4563aaf41eefa
main.c aea392182bf4ab91370648fbfd657e1f13404190de6175a295a6224ff96c1673

Pie removed userspace FUSE from sdcard. Mocha's stock 3.10 kernel has FUSE but
no sdcardfs/esdfs. The standard Pie sdcard binary delegates to this daemon only
when ro.sys.legacy_fuse=true. ro.sys.sdcardfs=false selects its FUSE backend.
Vold still owns mounting/unmounting and process lifetime. Data is not migrated.
Accepts the sdcardfs-only -i option; userspace FUSE retains its existing
multiuser/package-derived permission handling.

Android 10 adaptation: accepts -o for per-user OBB directories; exposes the Q
runtime/full view with the same permission mask as platform sdcardfs; shares
inode notifications among all four views. The matching system/vold patch keeps
the userspace server alive after mount and reaps it on unmount, as in Pie.
