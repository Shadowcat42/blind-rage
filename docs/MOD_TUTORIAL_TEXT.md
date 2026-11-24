# Mod tutorial text

Agreed wording for the mod's own tutorial boxes, written with the player before implementation. The mechanism is described in `MOD_TUTORIALS_RESEARCH.md`.

Rules for all boxes:

- Basic descriptions only, no handholding.
- Boxes show every time their section comes up while the tutorial setting is on. Nothing records which were seen.
- Button names follow the player's controls (PlayStation, Xbox or keyboard bindings), like the game's own prompts. The text below uses PlayStation names.
- A box that demonstrates a game sound plays it after the box is confirmed, before the next box opens. The game is unpaused then, so the sound is not cut, and it never overlaps speech. The mod's own beacon can play at any time.

## Rail shooting (Episode 1 only)

Shown at Episode 1's first rail-shooting section, after the game's own shooting tutorial.

1. Welcome to Asura's Wrath! Before you get to punching, welcome to the wonderful world of the rail shooter. In these sections, the game moves Asura and the camera for you, along a set route. You don't steer, but you can dodge with cross, which later episodes may expect of you. You aim a reticle with the left stick and fire at what comes at you. Pressing or holding square fires a constant stream of fast, but weak shots. Triangle, though slower, fires a number of homing projectiles based off how many targets are currently locked... more on this next.
2. As you move your reticle over targets, they're locked onto. Think of it as marking multiple targets. You can only mark so many at once. You will know when a target is locked because of this beep, which gets higher as you mark more.
   - After confirming: the lock tone (`SE_UI_lockon1_Cue`).
3. When you hear this sound, it means you have maxed out the number of targets you can possibly hit. Press triangle to unleash your wrath and fire homing shots at all marked targets.
   - After confirming: the full lock tone (`SE_UI_lockon2_Cue`).
4. An audio beacon leads your aim to a group of targets. Left or right in your ears means left or right. Higher or lower pitch means above or below, respectively. The pulses speed up as your aim closes in, and become a steady tone when you're on target.
   - After confirming: the beacon's on-target tone for one second.
5. Pressing up on the D-pad will change your aiming mode: no guidance, the beacon, or the beacon with a wider locking cone than normal for slightly assisted aiming. Left or right will change which enemy cluster the beacon is pointing at, however you shouldn't need this often. Don't feel like manually aiming? Hold L1 for auto aim, and just blast away.

Evidence: the left stick moving the reticle and the two fire buttons come from Episode 1's own tutorial text; the lock limit differs between sections (the lock tone has versions at 4, 8 and 12 locks); the shooting controller keeps the normal combat movement, and dodging was confirmed in play. The set route and rapid fire being weaker come from play, not from the scripts.

Open implementation point: confirming a box unpauses the game while the next sound plays (under a second). If that matters at the start of Episode 1's section, start the sound and open the next box together instead, which needs a test of whether a sound started just before a pause keeps playing.
