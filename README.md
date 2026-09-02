# ByteStation

**ByteStation** is an experimental PlayStation 1 emulator written from scratch.

The project is primarily focused on understanding and reproducing the hardware and software behaviour of the original PlayStation rather than simply getting games to run. It is still heavily under development, and compatibility is currently limited.

<table>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/8abea8bf-afcd-4ba6-82e7-9e244d34f90d" width="500"></td>
    <td><img src="https://github.com/user-attachments/assets/b8524d17-81bb-4749-91bf-1f6eaaa5008a" width="500"></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/34e74798-f7a6-4b1e-8012-e895175f3606" width="500"></td>
    <td><img src="https://github.com/user-attachments/assets/d947986c-3083-4997-8a53-603de18c3c56" width="500"></td>
  </tr>
</table>

## Current State

ByteStation is not a very compatible emulator at the moment. Games boot and render but many titles have graphical bugs, missing functionality, timing issues and the occasional crash.

The idea is to improve compatibility gradually by implementing the hardware correctly, instead of doing game-specific hacks.

### Debugging

ByteStation includes development-oriented debugging functionality to make investigating games and hardware behaviour easier.

This is particularly useful when diagnosing incorrect CPU behaviour, GPU commands, memory accesses, and crashes.

## Compatibility

Compatibility is currently experimental.

| Game              | Status                       |
| ----------------- | ---------------------------- |
| Crash Bandicoot 3            | Boots                        |
| Pink Panther                 | Boots                        |
| Twisted Metal 4              | Boots                        |
| Mortal Kombat 2              | Boots                        |
| Pepsi-Man                    | Boots                        |
| Ridge Racer                  | Boots                        |
| Tekken 3                     | Boots                        |
| Twisted Metal 4              | Boots                        |
| Yu-Gi-oh! Forbidden Memories | Boots                        |
| Spyro the Dragon             | Boots                        |
| Crash Bash                   | Boots with some issues       |

A game successfully booting does **not** necessarily mean it is fully compatible. Rendering, audio, timing, controller input, save functionality, and other hardware behaviour may still be incorrect.

## Known Issues

ByteStation currently has many known problems, including:

* Timing inaccuracies
* Incomplete SPU emulation
* Incomplete CD-ROM behaviour
* Compatibility issues with some games

## Why?

The PlayStation is an interesting system to emulate because a large amount of its behaviour depends on interactions between relatively simple pieces of hardware.

A seemingly small difference in CPU timing, GPU behaviour, DMA transfers, interrupts, or memory accesses can result in completely different behaviour in a game.

ByteStation is therefore also a learning project: implementing the hardware, investigating unexpected behaviour, and using real games to discover where the emulation differs from the original hardware.

## Roadmap

### CPU

* [ ] Expand CPU debugging tools

### GPU

* [ ] Improve primitive accuracy
* [ ] Implement missing GPU commands
* [ ] Improve blending
* [ ] Improve dithering

### CD-ROM

* [ ] Improve command compatibility
* [ ] Improve sector handling
* [ ] Improve timing
* [ ] Improve error handling

### SPU

* [ ] Improve voice emulation
* [ ] Improve ADSR
* [ ] Improve pitch handling
* [ ] Improve reverb
* [ ] Improve timing

### System

* [ ] Memory card support
* [ ] Controller improvements
* [ ] Save/load state support
* [ ] Improve BIOS compatibility
* [ ] Improve overall timing

### Compatibility

* [ ] Expand game testing
* [ ] Track game-specific failures
* [ ] Investigate compatibility regressions
* [ ] Build automated compatibility tests
* [ ] Windows compatability

## Building

### Linux

```bash
git clone https://github.com/Daxy215/ByteStation.git
cd ByteStation

./build_release.sh
```

### Windows

Requires [vcpkg](https://github.com/microsoft/vcpkg) with `glfw3`, `sdl2` and `glew` installed, and `VCPKG_ROOT` set to your vcpkg install path.

```powershell
git clone https://github.com/Daxy215/ByteStation.git
cd ByteStation

./build_windows.ps1 -f
```

## Running

```bash
# TODO: ..
```

You will need a legally obtained PlayStation BIOS and game image.

## Contributing

ByteStation is an experimental project, and contributions, testing, bug reports, and investigations are welcome.

If you find a game that crashes or renders incorrectly, providing the following is particularly useful:

* Game title
* Region
* BIOS version
* Point where the problem occurs
* Screenshot or video
* Emulator log
* Whether the issue is reproducible

## Disclaimer

ByteStation is an independent project and is not affiliated with Sony Interactive Entertainment.

You must provide your own legally obtained BIOS and game software. No copyrighted BIOS or game data is distributed with ByteStation.

---

**ByteStation is currently a work in progress.**

It is nowhere near perfect yet, but the long-term goal is a reliable, accurate, and understandable PlayStation emulator.
