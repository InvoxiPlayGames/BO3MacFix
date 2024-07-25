# BO3MacFix

Fixes Black Ops 3 on Mac so custom maps from the Steam Workshop can be played.

by Emma / InvoxiPlayGames and Anthony / serious.

### Any issues you run into with custom maps or workshop mods should be reported here, and *not* to the mod's author!

## Install Guide

TODO. is not ready yet

## Credits

- [Anthony / serious](https://github.com/shiversoftdev) - guidance, help and the cod2map64 patch DLL.
- [The Dobby project](https://github.com/jmpews/Dobby) - code hooking on macOS. (used under the MIT license)

## What does it do? (Nerd stuff)

There are two big issues with Steam Workshop map loading in the current (v98, from 2019) Mac build of BO3:

* Texture (.png) conversion crashes the game when hovering or selecting a workshop map
* Havok navmesh/navvolume files are a different format across PC and Mac

This project hooks into the game to solve both these problems. It solves the texture problem by stubbing out
the faulty function, and solves the navmesh problem by converting it on-the-fly using a modded version of the
modding SDK. We call Wine from within the native Mac game process to call upon the modding SDK.
