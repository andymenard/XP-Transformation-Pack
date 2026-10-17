# Font rendering

XP desktop controls explicitly select their interface fonts. Generic sans-serif,
serif and monospace families retain the distribution's normal font preferences,
so browsers and document viewers do not silently inherit Tahoma or XP smoothing.

XPlasma's fontconfig policy applies full hinting and the fonts' legacy size-based
smoothing only to Tahoma, Trebuchet MS and Lucida Console. The separate opt-in
pixel-font rules remain scoped to their explicit pixel families. Arial, Times
New Roman and other imported document fonts retain modern system rendering.

The installer no longer copies the broad legacy xp-fonts.conf payload. Native
updates migrate the original installer rule and unchanged global Xft overrides;
customized legacy files and later user preference edits are preserved. The
maintenance refresh checks the policy version even when display scale is
unchanged.

Fully quit and reopen apps after applying the update. Running apps can cache
font selection and rendering settings. A logout/login may be needed to refresh
all desktop processes.

A useful check on affected systems is:

```bash
fc-match -v sans-serif
fc-match -v 'Arial:pixelsize=14'
fc-match -v 'Tahoma:pixelsize=11'
```

On the reported machine, the repaired generic family resolves to Noto Sans with
antialiasing enabled; Arial is also smoothed, while Tahoma at 11 pixels retains
its crisp XP interface rendering.
