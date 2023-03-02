# Nox Archaist Companion

Welcome to Nox Archaist Companion!
It is a fully self-contained Windows 10+ application for running Nox Archaist, the great Apple 2e RPG of 2020.
It goes beyond simple emulation, and provides you with configurable sidebars to display all sorts of useful in-game information, as well as an auto-log which continuously copies conversations into a window that you can edit, translate, save...

Nox Archaist Companion also provides GameLink support for Grid Cartographer, a commercial program that helps you map as you play (or use existing maps).

Nox Archaist Companion's emulation layer is based on AppleWin, the great Apple 2 emulator. This program would never have seen the light of day without the amazing work of the AppleWin developers.

This app requires DirectX 12 and Windows 10 x64, with working sound.
Fork on github or contact me for other builds, but the DirectX 12 requirement is set in stone, sorry.

## Installation

- Copy the Nox Companion folder anywhere you want, ideally in your Program Files directory.
- Run the companion .exe. The first time you run it, it will ask you to select the Nox Archaist .HDV file you want to use. It will remember it for future sessions.

## Running the program

The Companion embeds a heavily modified version of AppleWin 1.29.16, and will automatically load Nox Archaist after you select the Nox .HDV file.
You can optionally load the profile of your choice (they're located in the Profiles directory), and play the game. Make sure you use profiles that match the version of the game that you are playing, otherwise they will probably show the wrong data. You can find the version that you're playing by going into the game's main menu screen and selecting "About Nox Archaist".

There are a number of emulation options, all directly accessible from the main menu bar. Playing at max speed automatically disables sound, and it will generally play so fast that you won't see battle animations. This is highly discouraged unless you've become very adept at the game.

## Profiles

The key feature of the Companion is its profiles. A profile is a JSON document that specifies what the Companion should display, and where. The Companion has support for many types of data in memory, including being able to translate numeric identifiers into strings (something very useful when you want to show "Short Sword +1" instead of 0x0b).

The documentation for profiles is sorely lacking, but I've included a number of profiles for Nox Archaist. Feel free to experiment and ping me for more info.

## Grid Cartographer Gamelink

The Companion provides support for Grid Cartographer's Gamelink feature. Grid Cartographer is a commercial app <https://store.steampowered.com/app/684690/Grid_Cartographer/> for map creation, and Nox Archaist Companion can be used alongside it to significantly improve your mapping experience. If you own GC, run it, and go to Game Browser > Auto Tracking > Import. When the filesystem browser shows up, point it to your Nox Archaist Companion folder, inside Profiles > Grid Cartographer Profiles, and select the file there.

Once this one-time setup is done, whenever you run the Companion and GC, you'll be able to map your way through the game while always knowing where you are, and in which region.

## Logging

The Companion automatically logs all conversations, and optionally combat. When logging combat, you should probably activate the "combat math" by pressing SHIFT+8 while in combat (this is a Nox Archaist Feature).
You can show the log file from the menu. You can copy, paste, or otherwise modify the text within the log window. You can (also from the menu) load and save the log.

WARNING: Save or copy the log window content somewhere else before you quit the app or it is lost forever!

## Cheating

There's a Hacks window you can access via the Companion menu. Use at your own risk. :)

Happy retro RPG gaming, and see you on the Nox Archaist Discord server: http://discord.noxarchaist.com

Rikkles, Lebanon, 2021-2023.


@rikretro on twitter
https://github.com/hasseily/NoxArchaistCompanion
