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

## QTE note (Episode 1 only)

Shown when Episode 1's first QTE appears (in a run where its rail-shooting section was reported), before that QTE is announced. The game is frozen with the QTE on screen; after the box is confirmed the QTE is announced as usual. If the box has not opened within 0.4 seconds, the note is dropped and the QTE is announced at once.

1. Central to the Asura's Wrath experience are many, many QTEs, all of which are announced. Be ready! You could be asked to press something, multiple somethings or do something with either stick at any time.

## Melee (Episode 2 only)

Shown when Episode 2's first melee section starts (`BC-S010`, `PDEMode` 1). Episode 3 has the game's full melee tutorial, so this only introduces the basics. No box plays a demonstration.

1. At last, you get to punching! (While loudly insisting you didn't kill the Emperor.) In these sections you move Asura freely with the left stick. Press circle to attack, and keep pressing for a combo. Episode 3 covers the rest in full.
2. On their own, your attacks only turn Asura toward an enemy that's close and roughly in front of him. Anything farther away or behind you gets missed. Press L1 to lock on to the nearest enemy, and your attacks will follow it wherever it goes. Press L1 again to let go.
3. Hold circle for a dash attack: Asura charges in, turned toward his target. Combined with lock-on, it's a quick way to get to an enemy that's out of reach.
4. A melee beacon leads you to enemies. Left or right in your ears means left or right; a higher tone means ahead of you, a lower one behind. It points to the nearest enemy and pulses faster as you close in. It turns into a steady tone once an enemy is in reach, which means your attacks will turn toward it. Press up on the D-pad to turn the beacon off or back on.

Evidence: the attack, combo, dash attack and lock-on buttons come from the game's own tutorial text (`S010_Assault`, `S020_DashAssault`, `S020_TLock`). Automatic targeting picks only enemies within 500 units in line of sight, favouring those in front, and attacks turn Asura to that target as they start; L1 searches 5000 units for the nearest enemy, and combo follow-ups keep facing a locked target ([gameplay systems](GAMEPLAY_SYSTEMS.md)). The dash attack plays Asura's `Chg01` animations with root translation kept: in Episode 9's animation set the start moves the root about 257 units and the finishing strike about 839 along the axis every attack moves on, against about 328 for the first combo hit; the hold loop stays in place. That axis being forward is an inference from all attacks using it. The game's own attack box (`S010_Assault`) follows a one-second Kismet delay after the section starts; it is an inference that the mod's pause also holds that delay, so it appears after the mod's boxes. Not yet seen in play.

## Episode 8 chase

Shown when Episode 8's chase starts (`BC-S070`, a shooting section whose controller has the game's lock-on off).

1. This is a chase. Like rail shooting, the game moves Asura forward for you, but there is no lock-on here, and pushing left or right moves Asura himself across the path as well as your aim. Hold square to fire at the enemy you're chasing.
2. The enemy fires blasts that skim along the ground towards you, and it fires more as your Burst gauge fills. This buzz warns you. Centred means it's coming straight at you, so move aside or dodge with cross. Off to one side means it will pass on that side.
   - After confirming: the warning buzz for two seconds, sliding from centre to the right.
3. When the enemy drops back to you, the game tells you to attack: hit it with circle for combos. Fill your Burst gauge and press R2 to finish the chase. The beacon here points only at the enemy. Hold L1 to auto aim up and down; left and right stay with you, since they also move Asura.

Evidence: the missing lock-on, the cursor moving Asura sideways, fire skimming the ground and increasing with Burst, and the attack prompt are from the game's scripts and level data; square rapid fire and Burst with R2 ending the chase are from play. Dodging with cross was confirmed in play, in this chase as well as Episode 1's rail shooting.

## Fight tips

Separate from the tutorials and switched by their own setting (`mod_tips`). A tip covers only what a blind player can't work out alone, and leaves the rest, such as which attacks work, to discover. Each belongs to one episode's map and one enemy, and is shown when that enemy appears there during melee, so the same enemy elsewhere brings no tip.

### Episode 7's giant elephant Gohma

Shown when `BCPawn_zou01` appears (`BC-S060`'s second section). The third section's Gohma gets no tip.

1. This giant elephant Gohma can't just be beaten down. Watch for sweeping blows, fireballs that come down where you were standing, and a huge stomp whose shockwave lands under you. Right after the stomp it's open to a special attack, so watch for the prompt. Once your Burst is half full it gets angry, and each volley becomes a rain of fireballs. Good luck.

Evidence: the Gohma is spawned invincible and the section ends on Burst (level script). `BCAI_EnemyBossZou01` attacks in a fixed order: combo, shot, combo, Vajra attack, shot. Shots (`BCProj_Zou`) rise and come down on the player's position when fired, marked on the ground. The Vajra attack's shockwave (`CreateVajraShockwave`) lands at the player's ground position, and just before it the Gohma's targets accept finishing attacks (`ExecBehaviorAttackZou`, `SetIgnoreFinishAttack(false)`). At 50% Burst (`EmotionHealthList`, read from the player's Mantra gauge in `CheckEmotion`) it turns angry, and each shot becomes 10 fireballs (`ExecBehaviorShoot`).

### Episode 11's Augus after the eyecatch

Shown when `BCPawn_ogs02` appears (`BC-S110`'s second section). Augus's first section (`BCPawn_ogs01`) gets no tip.

1. Augus has decided he wants to become a zoner, keeping his distance and fighting with sword waves. A buzz means a blade is about to hit you: step toward the side it's on, and it stops once you're clear. A quick rising tone means a wide wave is coming: jump when the tone ends.

Evidence: far away `BCAI_EnemyBossOgs02` only fires waves, and within 500 units it dashes to a far point (`ExecBehaviorSwiftlySlash`). The buzz and the rising tone are the mod's sword-wave guide ([melee aiming](MELEE_AIM.md#sword-waves)).
