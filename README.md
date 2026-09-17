# Dad Jokes for PebbleOS

A native C application optimized for the modern 2026 Core Devices Pebble Time 2. 

## Features
* **Massive Library:** Over 190+ classic dad jokes loaded directly into memory.
* **Modern Hardware Support:** Full capacitive touch scrolling for the PT2 (Emery architecture) alongside legacy physical button support.
* **Custom PCM Synthesizer:** Features an entirely synthetic "Ba-Dum-Tss" rimshot and custom pitch-bending sound effects directly utilizing the firmware DAC—no external audio files required!
* **Clay Integration:** Configure font sizes, daily schedules, and alert styles directly from your phone.

## Build Instructions
1. Ensure the Core Devices 2026 Pebble SDK is installed.
2. Run `pebble package install` to grab the `pebble-clay` dependency.
3. Run `pebble build`.
4. Install to your watch: `pebble install --phone <PHONE_IP>`
