# Dad Jokes for PebbleOS

A native C application optimized for the modern 2026 Core Devices Pebble Time 2. Dad Jokes is a lightweight, fully-featured entertainment app that dynamically loads humor from a local resource file and utilizes a custom software synthesizer to play punchline stingers.

## Features
*   **Dynamic Resource Loading:** Parses over 190+ classic dad jokes dynamically loaded into memory from a plain text file, keeping the memory footprint incredibly low.
*   **Modern Hardware Support:** Full capacitive touch scrolling for the PT2 (Emery architecture) alongside legacy physical button support.
*   **Custom PCM Synthesizer:** Features an entirely synthetic audio engine directly utilizing the firmware DAC. It uses `psleep` to yield the CPU for exact millisecond durations without locking up the watch[cite: 1].
    *   *Classic Beep:* A 4096 Hz double-chirp mimicking vintage digital watches.
    *   *Ba-Dum-Tss:* A staccato rimshot utilizing an 8th note and two quarter notes mixed with a white-noise crash.
    *   *Circus March:* A precise 6-note chromatic descent.
    *   *Fart:* A randomized frequency modulation sequence.
*   **Native UI Elements:** Implements the OS-level `ScrollLayer` for smooth pagination and the `ContentIndicator` subsystem for crisp, animating vector arrows[cite: 1].
*   **Pebble-Clay Integration:** Configure font sizes, daily schedules, timeouts, and alert styles directly from the companion app on your phone.
*   **Watchdog Fail-Safes:** Includes embedded safeguards to prevent "soft-bricks" by enforcing minimum timeout durations before the application allows itself to close.

## Build Instructions
1. Clone this repository to your Gentoo Linux workstation.
2. If you are modifying the C source in vim, ensure you have the Core Devices 2026 Pebble SDK in your path.
3. Run `pebble package install` to grab the `pebble-clay` dependency.
4. Run `pebble build` to compile the `.elf` binary.
5. Install to your watch: `pebble install --phone <PHONE_IP>`

## Credits
The dad jokes included in the `jokes.txt` resource file were sourced from [Bustle's Best Dad Jokes compilation](https://www.bustle.com/life/best-dad-jokes).
