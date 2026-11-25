VORTEX for the Shanling Q2 (Q2 Pod)
===================================

Breakout down a tube. The bricks line the inside of a tube in rings that turn; the ball runs round
the wall, down to the far end and back. Keep it in play with your bat on the near rim and clear
every brick to open the door at the end and go on. 40 levels, then the game makes up its own.

The game's art, levels, words and sound come from your own copy of the 2006 click-wheel iPod
game "Vortex" (the .ipg file iTunes kept). They aren't included: you supply the file.

INSTALL
1. Install it from Library -> App Store -> Games on the Q2, or copy the .vortex folder to the
   root of the card. On a Mac, press Cmd-Shift-. in Finder to see dot-folders.
2. Copy your Vortex .ipg into .vortex (any file name ending .ipg; it's read, never changed).
3. Eject the card and open Library -> Games -> Vortex.

CONTROLS
  Wheel          move the bat round the tube (a turn of the wheel is a turn of the tube)
  Touch          with Options -> control: finger position, the bat follows your finger
  Next / Prev    move the bat while held
  Centre, a tap  launch the ball; fire guns or the laser
  Play/Pause     pause (Return too): resume, options, help, or abandon the game
  Hold Return    leave the game (Q2 Pod's own)

In the menus the wheel turns the icons and Centre picks.

HOW TO PLAY
- Where the ball meets the bat steers it: the middle sends it straight down the tube, the ends
  send it round. Each return is a little faster, up to the level's top speed.
- Plain bricks take one hit; cracked-looking ones two or three. Grey bricks don't break.
  White bricks with red lights explode and take their neighbours with them (and set off other
  exploding bricks), so a whole ring can go at once.
- Green-windowed bricks drop a bonus: catch it on the bat. Extra life, multi-ball, a bigger or
  smaller bat, a second bat opposite, a sticky bat (Centre lets the ball go), guns, a laser,
  a power ball that goes through bricks, and slower or faster balls.
- Clear the level under the time to beat for a bonus.
- New game asks for a difficulty: it is where you start (newbie level 1, rookie 11,
  intermediate 21, pro 31, expert 41 and on, made up by the game).

SOUNDS
The iPod game's sound bank has no names, so which sound plays for what is a best guess.
Options -> sound test plays each by number. To change one, put a sounds.txt in .vortex with
lines like these (the names are bat brick crack solid bomb drop bonus lost over clear start
laser gun wall launch life shrink stretch move pick back door):
  bat=5
  brick=9
The iPod game's music is AAC, which the Q2 can't play for a card game, so there is none.

FILES
  q2-vortex     the game
  vortex.sav    your scores, totals, best times and options
  vortex.log    what the game did, for when something goes wrong
  sounds.txt    (yours, if you make one) which sound plays for what

Vortex for Q2's code is its own (MIT licence). It reads the iPod game's files only.
