
Debian
====================
This directory contains files used to package harvestd/harvest-qt
for Debian-based Linux systems. If you compile harvestd/harvest-qt yourself, there are some useful files here.

## harvest: URI support ##


harvest-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install harvest-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your harvestqt binary to `/usr/bin`
and the `../../share/pixmaps/harvest128.png` to `/usr/share/pixmaps`

harvest-qt.protocol (KDE)

