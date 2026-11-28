# Changelog

## Unreleased

- Counter and special attack prompts say the button first ("Triangle counter."), so you hear which button sooner when counters mix triangle and circle.
- The melee beacon goes quiet during counters and special attacks.
- Quick QTE wording (`qte_wording=quick`, also in the settings menu): "LS right RS left", "LS clockwise", "Mash L2 R2", without joining words, mash counts or "then release". Verbose stays the default.
- QTE freeze (`qte_freeze`, also in the settings menu): optionally pauses the game for up to 300 ms when a new button or stick QTE appears, so you hear it with a little more time to react. Presses during the freeze don't count. Off by default; Synchronic QTEs never freeze.
- Sword waves in an Episode 11 fight: a buzz points toward the gap when a blade in a fan would hit you, and a quick rising tone tells you when to jump a wide wave. Both start during the enemy's wind-up, read from the attack animation, so there's warning even up close.
- Synchronic count-ins keep all four beats when a key comes up with less lead than usual (several in Episode 11): the beats are spaced closer instead of the first being dropped.
- Fight tips, with their own setting (`mod_tips`): a short pausing box when an enemy whose fight isn't obvious appears. So far there are two, in Episodes 7 and 11.
- The walking beacon no longer turns you around on Episode 12's opening walk. It now prefers the triggers that move the walk on over ones that only react when you go back, so it leads straight on.
- F5 dumps taken during rail shooting now include the gameplay state; they used to say no player controller had been seen.

## 0.3.0

- Updates itself: on launch, BlindRage.exe checks for a newer Blind Rage release and offers it in a dialog that lists what changed since your version. It downloads the package, checks it against the release's checksum, replaces the mod's files (putting the old ones back if anything fails) and starts the new version. Settings are kept. Turn the check off with `update_check=0`. This is the first version with the updater, so updating to it from 0.2.0 is still done by hand.
- A spoken settings menu: Ctrl+O, or both stick clicks on a controller. It pauses gameplay; changes apply at once and are saved.
- Episode 8's chase: the beacon points at the enemy, a buzz warns of incoming fire and which side it passes on, and holding L1 aims up and down. Three tutorial boxes introduce it.
- Start no longer opens the pause menu while one of the mod's tutorials is running.
- Episode 2 tutorial: four pausing boxes introduce melee combat, lock-on, the dash attack and the melee beacon when the first fight starts.
- A look-around beacon for scenes where the right stick turns the camera, such as Episode 10's hot spring: it leads the view to each thing the game reacts to, so its look-around achievements are reachable.
- Separate volume for each cue: the shooting, walking, melee and look-around beacons, the fire warning and the Synchronic count-in, each a share of the overall cue volume, in the INI and the settings menu.
- F5 dumps now record the gameplay state (position, beacon triggers, enemies, burst guide target and nearby blocking volumes), not just menus.
- A melee beacon: a square wave an octave below the other beacons. In fights, it points to the enemy your attacks will turn to, steady once it's in reach, or else to the nearest enemy. D-pad Up or M toggles it outside rapid fire; `melee_beacon=0` turns it off.
- The walking beacon is now a sine tone, so it sounds different from the shooting and melee beacons.
- The walking beacon points at doors the game marks for Burst, such as Episode 9's last door, where the R2 prompt only appears close to the door.
- Back/Select reads Health and Burst in scripted scenes that show the gauge, such as Episode 10's hot spring.
- The mod's tutorials now reliably appear in the first section they belong to, and the QTE note only follows Episode 1's rail tutorial.

## 0.2.0

- Accessible first-time setup: when the game isn't installed, BlindRage.exe sets it up in its console window through the PC version's own installer. It downloads the program files, installs the game from your disc image with spoken progress, and offers to add the title update or DLC. The "Blind Rage setup" shortcut it creates adds them later.
- Episode 1 tutorials: five pausing boxes introduce rail shooting and the mod's aiming help, with the lock tones and the beacon demonstrated, and a short note comes before the first QTE. Turn them off with `mod_tutorials=0`.
- The Episode menu reads the part name ("Part II: Rebirth") instead of "Episode Menu".
- Settings are no longer in the package, so updating never resets them: BlindRage.exe creates `accessibility.ini` on first launch and adds new settings later, keeping your values.
- A clear message when BlindRage.exe is copied without the rest of the package.
- Smaller logs: each menu read is no longer logged.

Episodes 1 to 7 have been played with the mod.

## 0.1.0 (first public release)

First public release for the Mark2 2026.11.01.1627 builds of the Asura's Wrath PC recompilation (title update recommended).

- Spoken menus: settings, episode select, records, results, collections, confirmations and Control Settings, including the Custom key editor.
- Spoken subtitles, tutorials and quick-time events, with button names for your controller or key bindings.
- Synchronic count-in beeps; finisher and counter prompts.
- Health, Burst and Unlimited status on Back/Select or H, with low-health warnings.
- Audio beacons and L1 tracking for rail shooting and melee rapid fire; optional boosted lock-on.
- Walking-section beacon toward the next story trigger.
- Controller is the primary way to play; keyboard support is in progress.

Episodes 1 to 5 have been played with the mod. See the README's known issues.
