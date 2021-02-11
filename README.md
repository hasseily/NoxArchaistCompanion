# Nox Archaist Companion

Welcome to Nox Archaist Companion!
It is a fully self-contained Windows 10 application for running Nox Archaist, the great Apple 2e RPG of 2020.
It goes beyond simple emulation, and provides you with configurable sidebars to display all sorts of useful in-game information, as well as an auto-log which continuously copies conversations into a window that you can edit, translate, save...

Nox Archaist Companion also provides GameLink support for Grid Cartographer, a commercial program that helps you map as you play (or use existing maps).

Nox Archaist Companion's emulation layer is based on AppleWin, the great Apple 2 emulator. This program would never have seen the light of day without the amazing work of the AppleWin developers.

This app requires DirectX 12 and Windows 10 x64, with working sound.
Fork on github or contact me for other builds, but the DirectX 12 requirement is set in stone, sorry.

## TODO: EVERYTHING BELOW NEEDS TO BE REWRITTEN

## Installation

- Install `vc_redistx64.exe` which is included in the release zip.
- Copy the Nox Companion folder anywhere you want, ideally in your Program Files directory.
- Go inside the folder and copy your Nox Archaist .hdv file into it, renaming it to NOXARCHAIST.HDV. This file should be in the same directory as the file "Run Nox Archaist"
- Double-click on "Run Nox Archaist"

## Running the program

The companion is made to be the primary window from which to run your game. When launching "Run Nox Archaist", it will automatically launch AppleWin minimized without video, load NOXARCHAIST.HDV and run it. It will also run the Companion app and both will link up, showing the video in the Companion app.

Load the profile of your choice (they're located in the Profiles directory), and play the game.

If you want to set a specific video mode such as monochrome or RGB, go to the running AppleWin (which should be minimized) and press F8.

## Profiles

The key feature of the Companion is its profiles. A profile is a JSON document that specifies what the Companion should display, and where. The Companion has support for many types of data in memory, including being able to translate numeric identifiers into strings (something very useful when you want to show "Short Sword +1" instead of 0x0b).

The documentation for profiles is sorely lacking, but I've included a number of profiles for Nox Archaist. Feel free to experiment and ping me for more info.

## Logging

The Companion automatically logs all conversations. You can show the log file from the menu. You can copy, paste, modify the text within the log window. You can also from the menu load and save the log.

WARNING: Save or copy the log window content somewhere else before you quit the app or it is lost forever!


Happy retro RPG gaming, and see you on the Nox Archaist Discord server: http://discord.noxarchaist.com

Rikkles, Lebanon, 2021.


@rikretro on twitter
https://github.com/hasseily/AppleWin-Companion
