# ShoWin 2.00 — Decompilation & Reconstruction

**ShoWin** is a small Win32 utility originally written by **Robin Keir** and later acquired by [Foundstone](https://en.wikipedia.org/wiki/McAfee#Foundstone), a well-known security consulting firm, around the year 2000. The version numbering reflects this transition — as the readme bundled with the release puts it:

> *"Version 2.00 does not represent any additional functionality to previous versions of ShoWin, it simply signifies the fact that ShoWin has been acquired by Foundstone."*

Foundstone was later acquired by McAfee in 2004.

From the [original Foundstone product page](https://web.archive.org/web/20031104130047if_/http://www.foundstone.com:80/index.htm?subnav=resources/navigation.htm&subcontent=/resources/proddesc/showin.htm):

> *ShoWin displays useful information about windows by dragging a cursor over them.*
>
> *Perhaps one of the most popular uses of this program is to display hidden password editbox fields (text behind the asterisks \*\*\*\*\*). This will work in many programs although Microsoft have changed the way things work in some of their applications, most notably MS Office products and Windows 2000. ShoWin will not work in these cases. Neither will it work for password entry boxes on web pages, at least with most web browsers.*
>
> *Additional features include the ability to enable windows that have been disabled, unhide hidden windows (try the program with the include invisibles option set and see how many windows you have on your desktop that you didn't know about!) and force windows to stay on top or be placed below others.*

The original binary can still be downloaded from the Internet Archive:
**[showin.zip](https://web.archive.org/web/20060104123943/http://www.foundstone.com/resources/freetooldownload.htm?file=showin.zip)**

ShoWin has not been maintained or distributed commercially since the early 2000s, placing it firmly in abandonware territory.

## About this project

This is an AI-assisted decompilation and reconstruction of `showin.exe` (version 2.0.0.0, Copyright © 2000 Foundstone, Inc., timestamp 2000-11-13, 24 KB). The goal is software preservation and education.

The binary was decompiled using a disassembler, and the resulting pseudocode was cleaned up and corrected with the help of an AI assistant. The bulk of the work — resolving magic numbers, naming data structures, and restoring x64 compatibility — is done. The reconstructed source compiles and runs on modern Windows. There is still room to improve: the code can be made more idiomatic, and the tool itself could be updated to align with modern OS conventions such as HiDPI scaling and dark mode theming.

## Legal note

Decompilation for interoperability, research, and education is widely considered lawful under US copyright law (17 U.S.C. § 107 — fair use) and EU Directive 2009/24/EC Article 6. No original compiled code is redistributed. If Foundstone, McAfee, or any rights holder objects, please open an issue and it will be taken down promptly.

## Building

Requires [xmake](https://xmake.io) and Visual Studio 2022.

```
xmake build
```

## Usage

Run `showin.exe`. Drag the crosshair icon over any window to inspect it. The dialog updates in real time with the target window's properties.

## Similar tools
<details>
<summary>Similar tools</summary>

Microsoft ships **[Spy++](https://learn.microsoft.com/en-us/visualstudio/debugger/spy-increment-help)** (`spyxx.exe`) with Visual Studio — it covers similar ground and adds message logging and a window tree view. The Windows SDK also includes **[Inspect](https://learn.microsoft.com/en-us/windows/win32/winauto/inspect-objects)** (`inspect.exe`), which focuses on the accessibility (UI Automation) tree and is the successor to the older UI Spy tool.

On macOS, Xcode ships **Accessibility Inspector**, which exposes the same kind of metadata through the macOS accessibility APIs. GTK applications on Linux have the built-in **GTK Inspector** (enabled with `GTK_DEBUG=interactive`), which provides a live widget tree and property editor. In the Qt/KDE world, **[GammaRay](https://github.com/KDAB/GammaRay)** by KDAB is the equivalent — it can introspect Qt objects, signals, models, and rendering at runtime, and works cross-platform.

For accessibility specifically, GNOME has **[Accerciser](https://wiki.gnome.org/Apps/Accerciser)**, an AT-SPI2 tree explorer used to verify that applications expose the right information to screen readers. Microsoft also ships the open-source **[Accessibility Insights](https://accessibilityinsights.io/)**, a more modern complement to Inspect.exe with guided test workflows.

On X11, the classic command-line counterparts are **`xprop`** and **`xwininfo`** — simple but still widely used when you just need a quick look at a window's properties or geometry without opening a GUI tool.

</details>
