
Debian
====================
This directory contains files used to package historiad/historia-qt
for Debian-based Linux systems. If you compile historiad/historia-qt yourself, there are some useful files here.

## historia: URI support ##


historia-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install historia-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your historia-qt binary to `/usr/bin`
and the `../../share/pixmaps/historia128.png` to `/usr/share/pixmaps`

historia-qt.protocol (KDE)

