NOTE: please fill in the first section with information about your game.

# *Cube Volleyball*

*Cube Volleyball* is *Uday Uppal*'s implementation of [*Cube Volleyball*](http://graphics.cs.cmu.edu/courses/15-466-f17/game3-designs/rmukunda/) for game3 in 15-466-f17.

![Image](screenshots/screenshot.png?raw=true "Image")

## Asset Pipeline

The asset pipeline consists of a blender file called cube_volleyball.blend and an export-meshes.py script used to export information from the blender file into usable scene and mesh objects. These objects can then be created and controlled through their transformations in game.

## Architecture

The architecture is based on the base2 code. Scene objects are created to represent the two players and the ball, and these are updated based on key events. Each has a position and velocity that is changed constantly based on collisions and acceleration. 
In the game state update section, collisions are detected to change the velocities and positons if necessary, then new positions are calculated using the new velocities. At the end of each loop, the game state is checked again to see if a new round should be started.

## Reflection

Originally, getting the collisions to work correctly was fairly difficult. I kept miscomputing the collision areas and velocity recomputations. Edge cases were also difficult to fix sometimes since collisions could lead to objects getting "stuck" inside each other. If I were doing this again, I would focus on writing cleaner collision code by using better variables. The game logic however worked well, and score tracking and movement never gave me any issues.

The design document was fairly open to interpretation and customization. It made the objective of the game, the game over state, and the controls clear. However, it was unclear what counts as a bounce in the game, and how the physics of the system should work. For example, it did not clarify if the players should only change the direction of the ball's velocity, or also the speed and acceleration. To resolve these ambiguities, I made it so that the ball starts with an initial velocity towards the side player that lost the previous point (or the left player for round 1). Then, the players can only influence the direction of the ball by bouncing it perpendicular to the cube faces, without changing the speed. Also, I decided that bounces off the players as well as the floor would count as "true bounces" towards the 4 bounce limit, but bounces off the side walls would not. I felt that these decisions would make the gameplay most intuitive.

# About Base2

This game is based on Base2, starter code for game2 in the 15-466-f17 course. It was developed by Jim McCann, and is released into the public domain.

## Requirements

 - modern C++ compiler
 - glm
 - libSDL2
 - libpng
 - blender (for mesh export script)

On Linux or OSX these requirements should be available from your package manager without too much hassle.

## Building

This code has been set up to be built with [FT jam](https://www.freetype.org/jam/).

### Getting Jam

For more information on Jam, see the [Jam Documentation](https://www.perforce.com/documentation/jam-documentation) page at Perforce, which includes both reference documentation and a getting started guide.

On unixish OSs, Jam is available from your package manager:
```
	brew install ftjam #on OSX
	apt get ftjam #on Debian-ish Linux
```

On Windows, you can get a binary [from sourceforge](https://sourceforge.net/projects/freetype/files/ftjam/2.5.2/ftjam-2.5.2-win32.zip/download),
and put it somewhere in your `%PATH%`.
(Possibly: also set the `JAM_TOOLSET` variable to `VISUALC`.)

### Bulding
Open a terminal (on windows, a Visual Studio Command Prompt), change to this directory, and type:
```
	jam
```

### Building (local libs)

Depending on your OSX, clone 
[kit-libs-linux](https://github.com/ixchow/kit-libs-linux),
[kit-libs-osx](https://github.com/ixchow/kit-libs-osx),
or [kit-libs-win](https://github.com/ixchow/kit-libs-win)
as a subdirectory of the current directory.

The Jamfile sets up library and header search paths such that local libraries will be preferred over system libraries.
