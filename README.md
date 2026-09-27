# Dad Jokes for PebbleOS

A charming, highly configurable Pebble smartwatch app that delivers a healthy dose of groans right to your wrist. Version 1.4 brings Rebble Timeline integration, advanced polyphonic audio alerts, and deep customization to both modern Pebble hardware and classic devices.

## Features

* **Universal Hardware Support:** Beautifully laid out for all Pebble models—including the round screens on Chalk (Time Round) and Gabbro (CoreDevices PR2). On round displays, the app utilizes Smart Text Flow to curve text around the physical bezel so your punchlines are never clipped.
* **PebbleOS Timeline Integration:** Opt-in to receive a daily "Joke of the Day" pushed directly to your Pebble Timeline for a quick laugh without opening the app.
* **Custom Joke Rosters:** Don't just rely on the built-in puns. Use the Pebble app settings to paste in your own custom list of jokes (one per line). Choose to use only built-in jokes, only your custom list, or shuffle them both together.
* **Flexible Delivery Schedules:** 
  * *Specific Time:* Set an exact time (e.g., 12:38 PM) to receive your daily joke.
  * *Random Window:* Specify a block of time (e.g., between 9:00 AM and 5:00 PM) and how many jokes you want per hour. The app will intelligently deliver them at randomized, staggered intervals.
* **Quiet Time Aware:** The app automatically respects your Pebble's system-wide "Quiet Time" (Do Not Disturb) settings. If Quiet Time is active, scheduled jokes will silently display on the screen without triggering vibrations or audio.
* **Flexible Input & Flick-to-Dismiss:** Use the Up/Down buttons to scroll through longer jokes and the Select button to instantly load the next one. Done laughing? Just flick your wrist. The app utilizes the Pebble accelerometer to automatically close the joke and return you to your watchface (can be toggled on/off in settings).
* **Light & Dark Mode:** Toggle a high-contrast Dark Theme in the settings for a sleek white-on-black interface that is easy on the eyes.
* **Custom Polyphonic Audio Alerts:** Exclusively for Pebble Time 2 and CoreDevices hardware with built-in speakers. Choose your punchline alert, or set it to "Random" for a fresh surprise (with a 1-hour anti-repeat guarantee):
  * Classic Digital Beep
  * Ba-Dum-Tss (Rimshot)
  * Super Mario Bros Theme
  * Mexican Hat Dance
  * Entrance of the Gladiators (Circus Theme)
  * The Legend of Zelda Theme
  * Fart
* **Volume Override:** Need a quieter laugh? Override the system default volume directly inside the app's settings.

## Usage

1. Open the app from your Pebble launcher to view a joke.
2. If the joke is long, use the **Up** and **Down** buttons to scroll. 
3. Press the **Select** (middle) button to fetch a new joke from your phone.
4. **Flick your wrist** (or press the Back button) to exit and return to your watchface. 

*Note: Dad Jokes dynamically fetches content from your phone to save storage space on your watch. An active Bluetooth connection is required to load new jokes.*

## Configuration

All settings are seamlessly managed via the standard Pebble mobile app using Pebble-Clay (with HTML fallback). 
1. Open the Pebble app on your phone.
2. Navigate to your Apps list and select **Dad Jokes**.
3. Tap **Settings**.
4. Customize your schedule, audio, volume overrides, UI theme, wrist-flick behavior, and paste in your custom jokes.
5. Tap **Save Settings** to instantly sync your changes to the watch.

## Compatibility
Fully compatible with PebbleOS 3.0+ and 4.0+. Tested and optimized across Aplite, Basalt, Chalk, Diorite, and Emery (Pebble Time 2 / CoreDevices) platforms.

## License
Open-source under the MIT License.
