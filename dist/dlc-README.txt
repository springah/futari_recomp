DLC folder

Put your own Xbox 360 DLC packages for Mushihimesama Futari in this folder,
filenames unchanged. They are the STFS container files (they start with the
bytes "LIVE", "CON " or "PIRS") that live under your console's storage at
Content\0000000000000000\435A07D7\00000002\. docs/DUMPING.md in the source
repository explains how to copy them off the console; when you point the
extractor at a console install folder it copies them here for you.

The game installs whatever it finds here on every launch (the log shows
"DLC: InstallContent(...) -> 0x00000000" for each one) and the content then
appears as owned. An empty folder is fine: the game runs without DLC.

Nothing in here is distributed with the port. Only your own copies go here.
