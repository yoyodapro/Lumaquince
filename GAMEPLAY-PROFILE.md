# Recreating the Ocarina gameplay profile

The application starts with a clean data location. No personal controls, profile,
saves, keys, firmware, game library or shader cache is supplied. Configure your
own files and controller first.

These generic settings reproduce the graphics/core choices used in the successful
development profile. They are recorded settings, not a universal benchmark or a
promise that every PC or scene benefits. Use game Properties where available so
the choices do not affect unrelated games.

| Setting | Development profile |
|---|---|
| Graphics backend | Vulkan |
| Resolution scale | 0.75x / three quarters |
| GPU accuracy | Low |
| Anti-aliasing | None |
| Window scaling filter | ScaleForce |
| VSync | FIFO |
| Asynchronous GPU emulation | Enabled |
| Asynchronous shader building | Disabled |
| Disk shader cache | Enabled |
| Reactive flushing | Enabled |
| CPU accuracy/backend | Auto / Dynarmic |
| Speed limit | Enabled, 100% |
| Core speed synchronization | Enabled |
| Fast CPU time override | 0 / normal |
| NVIDIA replacement DLSS and translated game DLSS network | Disabled |
| Adaptive Timing (Experimental) | Enabled for the exact supported build |

Low GPU accuracy is a performance choice and may affect rendering. Try High when
investigating image problems, and compare the same scene after shader activity
settles. Water strobing remains unresolved; this profile is not a fix for it.
Resolution scale trades detail for rendering cost. A larger scale may look sharper
and require more GPU time.

The shipped `windows/nx2_switches.txt` already selects split mode 2, fence batching,
draw budget 4096 and preload-all for previously encountered compatible pipelines.
Changing that file takes effect after a normal restart. Environment variables or
a user switch file can override it. Remove the preload-all override to use the
unchanged bounded used-list policy when startup or memory use is excessive.

The built-in timing Add-On changes a guest timing step; it does not increase
rendered FPS. Matching enabled external executable patches take precedence. If
moving from an old private timing experiment, do not assume its external mod must
remain enabled alongside the built-in option.

Game progress is separate from this public package. Back up before transferring
your own save, and do not replace newer active progress with an older copy. The
public release contains no save-transfer automation.
