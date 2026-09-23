"""Toolkit targets: where an applied theme's widget look goes.

Each target takes the same three calls -- a visual style applied, a Classic
scheme applied, everything undone -- and Apply and Reset walk them all in
order. Qt is real (the XPLasma style plugin, widget_style.py). GTK has no
style-plugin model, so it will always be *generated* -- from the manifest
`xplasma-render export` writes -- and until a generator exists its targets
say "not yet" through this same path: turning GTK on later means filling
in a stub, not changing the pipeline (PLAN.md section 5).
"""
from __future__ import annotations

import widget_style


class Target:
    """A toolkit whose widgets follow the applied theme."""
    name = ""
    label = ""

    def apply_visual_style(self, msstyles: str, variant: str | None, progress=print) -> None:
        raise NotImplementedError

    def apply_classic_style(self, scheme, progress=print) -> None:
        raise NotImplementedError

    def revert(self, progress=print) -> None:
        raise NotImplementedError


class QtTarget(Target):
    """Qt 5 and Qt 6 apps: the XPLasma style plugin."""
    name = "qt"
    label = "Qt"

    def apply_visual_style(self, msstyles, variant, progress=print):
        widget_style.apply_visual_style(msstyles, variant, progress)

    def apply_classic_style(self, scheme, progress=print):
        widget_style.apply_classic_style(scheme, progress)

    def revert(self, progress=print):
        previous = widget_style.remove()
        progress(f"  Qt: widget style restored to {previous}")


class NotYetTarget(Target):
    """A toolkit XPLasma will generate a theme for, but doesn't yet."""

    def __init__(self, name: str, label: str) -> None:
        self.name, self.label = name, label

    def _not_yet(self, progress) -> None:
        progress(f"  {self.label}: not yet (its apps keep their current theme)")

    def apply_visual_style(self, msstyles, variant, progress=print):
        self._not_yet(progress)

    def apply_classic_style(self, scheme, progress=print):
        self._not_yet(progress)

    def revert(self, progress=print):
        pass   # nothing was ever applied


TARGETS: list[Target] = [QtTarget(), NotYetTarget("gtk3", "GTK 3"), NotYetTarget("gtk4", "GTK 4")]


def _each(call, progress) -> None:
    """Run `call(target)` for every target; one target's failure (a checkout
    without a native build, say) is reported and the rest still run."""
    for target in TARGETS:
        try:
            call(target)
        except RuntimeError as exc:
            progress(f"  {target.label} skipped: {exc}")


def apply_visual_style(msstyles: str, variant: str | None, progress=print) -> None:
    _each(lambda t: t.apply_visual_style(msstyles, variant, progress), progress)


def apply_classic_style(scheme, progress=print) -> None:
    _each(lambda t: t.apply_classic_style(scheme, progress), progress)


def revert(progress=print) -> None:
    _each(lambda t: t.revert(progress), progress)
