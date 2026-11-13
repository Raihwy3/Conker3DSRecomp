# Controls and bottom screen

Options has five tabs: **Graphics, Controls, Audio, Interface and Misc**. Use the Circle Pad or D-pad to navigate, L/R to switch tabs, A to open a category or cycle a named option, B to go back, and Left/Right to adjust values. Tabs focus their first item immediately. Touch controls remain available.

In Options, use **Controls → Presets** to apply one of these layouts:

- **Classic:** the original camera, Circle Pad movement and the existing N64-style button mapping. Touch supplies the right-stick/C-button input.
- **Dual-stick:** Circle Pad movement, free camera on the C-stick, and right-stick aiming when looking or using a weapon. Touch supplies movement.
- **Touch look:** the same movement and aiming arrangement, with touch controlling the camera/right stick instead of movement. This also provides camera control without a C-stick.

- **Old 3DS:** Circle Pad movement, face-button camera/aiming (**X up, B down, Y left, A right**), with the displaced actions on the D-pad (**Up replaces X, Down replaces B, Left replaces Y, Right replaces A**). Shoulder buttons keep their standard actions. Diagonal camera input is supported. This also works on a New 3DS.

The first three presets restore the standard button bindings; Old 3DS moves the face actions to the D-pad. It leaves gyro, sensitivity, deadzones and graphics preferences alone. You can edit individual bindings and controls afterward; the preset then reads Custom. The chosen layout and its settings are saved between launches.

The face-button camera can also be toggled under **Controls → Camera → Face-button camera**. Native game menus follow the remapped actions (D-pad Right confirms and D-pad Down performs B). The launcher’s Options menu still uses its usual A/B and D-pad navigation.

Options captures game buttons, both sticks, touch input and gyro. After closing it, release the controls to resume input; a held close press or stick is not passed through to the game. This does not pause game simulation.

**Interface → Appearance** includes Lives display (Full, Icons only, Hidden), Chapter text (On/Off) and the existing Options button toggle. Icons only keeps one tail per spare life without the LIVES label. Chapter text hides both the chapter heading and scene title.

**Interface → Diagnostics → GFX task text** controls the bottom-screen capture-saved notification. Captures themselves and file logging remain available. The performance monitor has its own toggle.

On a New 3DS with the normal core layout, game logic runs on core 0, graphics work on core 2, and audio work uses the granted share of core 1. The SD read-ahead worker requests core 3 and falls back when that core is unavailable. It can spend most of its time asleep. CPU readings describe current workload, not a target that all cores must reach.

**Misc** holds recomp-specific modifiers. **Skip intro** skips the startup logos and chainsaw opening, loading the pub/menu entrance instead. It is saved for future launches and is off by default. This is a native adaptation of the upstream Skip Intro transition; no mod loader is used. It does not skip story cutscenes in a new game.

During the opening story’s map cutaways, the bottom screen keeps **Hungover — Introduction** rather than changing to each map’s chapter. Lives remain hidden until Conker regains control in the training area. The status screen does not display a launcher title.
