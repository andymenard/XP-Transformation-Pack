"""The single seam through which xp2plasma touches the outside world.

Every external command the app runs goes through this module, so that the
installer can be run from a packaged bundle (an AppImage) without the call
sites having to care.

The AppImage is not a sandbox -- it mounts its own Python, Qt and helper
binaries and then runs with the user's real filesystem, session bus and systemd
user manager. That removes the whole class of problem the Flatpak packaging had
to solve by tunnelling commands out through ``flatpak-spawn --host``. It does
*not* remove the need for this seam, because it introduces the opposite hazard:

* **The bundle's environment is not the host's.** The AppImage wrapper exports
  ``APPDIR``, ``APPIMAGE`` and library paths pointing into the mount, and the
  bundle ships its own ``python3``. Anything exported here is inherited by the
  host commands we then run, so ``PATH`` entries and ``LD_LIBRARY_PATH`` from
  the mount can make a host tool load our libraries, and can make
  ``install.find_host_python()`` return the *bundled* interpreter -- which would
  silently build the native venv with the wrong Python. ``_host_env()`` and
  ``_host_path()`` strip those back out on the way through, and
  ``_install_subprocess_guard()`` makes a call site that bypassed this module
  fail loudly rather than quietly doing the wrong thing.

* **We still need to know what the host has.** ``which()`` deliberately searches
  beyond the session's PATH (see ``_SUPPLEMENTAL_BIN_DIRS``), because a tool
  installed by Homebrew or Nix is real and runnable but routinely missing from a
  desktop session's PATH.

Three things here are load-bearing rather than stylistic:

* Captures are decoded as UTF-8 explicitly. With ``LANG`` unset Python picks
  ASCII and any non-ASCII path in a tool's output would raise.
* ``env=`` is always passed, never left to inherit. Inheriting is how the
  bundle's variables leak into host commands in the first place.
* ``which()`` is implemented with ``_execute()`` rather than ``run()``, because
  ``run()`` resolves ``argv[0]`` through ``which()`` -- see the comment there.
"""
from __future__ import annotations

import codecs
import os
import select
import shlex
import shutil
import subprocess
import threading
import time
from typing import Callable, Iterable, Sequence

import bundled_tools

# Captured before the guard below replaces it, so our own calls still work.
_subprocess_run = subprocess.run
_Popen = subprocess.Popen

# --- cancellation -------------------------------------------------------------
#
# A long job (the installer's pip step, a widget-style build) is a chain of
# commands. Registering an Event here makes every run() during that job
# interruptible: the running command is terminated and Cancelled is raised,
# which unwinds the job so it can roll back. Module-level rather than threaded
# through every caller, because setup_assets and widget_style shouldn't have to
# know they're being run from a wizard.

_cancel_event: threading.Event | None = None


class Cancelled(RuntimeError):
    """The current job was cancelled via set_cancel_event()."""


def set_cancel_event(event: threading.Event | None) -> None:
    global _cancel_event
    _cancel_event = event


def cancel_requested() -> bool:
    return _cancel_event is not None and _cancel_event.is_set()


def check_cancelled() -> None:
    """For jobs to call between steps that don't go through run()."""
    if cancel_requested():
        raise Cancelled("cancelled")


def _terminate(proc) -> None:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


# Running from the AppImage rather than from a source checkout or an installed
# copy. Set by the AppImage runtime itself, so it is reliable without our AppRun
# having to do anything.
IN_APPIMAGE = bool(os.environ.get("APPIMAGE"))
REAL_HOME = os.path.expanduser("~")

# Variables that describe the *bundle*, not the host, and must never be handed
# to a host command.
#
# PATH is handled separately by _host_path(), which subtracts the mounted
# AppDir rather than dropping PATH wholesale.
_APPIMAGE_ENV_VARS = (
    # Set by the AppImage runtime / AppRun.
    "APPDIR", "APPIMAGE", "OWD", "ARGV0", "APPIMAGE_EXTRACT_AND_RUN",
    # Pointing into the mount: a host binary that honours these loads our Qt,
    # our Python or our bundler's preload instead of its own.
    "LD_LIBRARY_PATH", "LD_PRELOAD", "PYTHONHOME", "PYTHONPATH",
    "PYTHONSTARTUP", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH",
    # XDG_*BASE_DIR point at real user directories and are deliberately left
    # alone. XDG_*_DIRS are *search* paths, though, and the AppImage prepends
    # its own share dirs to them -- which would make a host tool pick up
    # desktop entries or icons out of our mount. See _host_env().
    "XDG_DATA_DIRS", "XDG_CONFIG_DIRS",
)

# Tools whose absence is worth explaining rather than just reporting.
_DISTRO_HINTS = {
    "arch": "install the Plasma packages (plasma-workspace, kconfig, qt6-tools)",
    "debian": "install plasma-workspace, libkf6config-bin and qt6-tools",
    "fedora": "install plasma-workspace, kf6-kconfig and qt6-qttools",
    "suse": "install plasma6-workspace, kf6-kconfig and qt6-tools",
}


class HostCommandError(RuntimeError):
    """A command failed, or the tool is not installed on the host."""

    def __init__(self, argv: Sequence[str], returncode: int, stderr: str = "", hint: str = ""):
        self.argv = list(argv)
        self.returncode = returncode
        self.stderr = stderr
        self.hint = hint
        detail = f"`{self.argv[0]}` exited {returncode}"
        if returncode == 127 or "not found" in stderr.lower():
            detail = f"`{self.argv[0]}` was not found on the host"
        message = f"{detail}: {' '.join(shlex.quote(a) for a in self.argv[:4])}"
        if stderr.strip():
            message += f"\n{stderr.strip()[-1500:]}"
        if hint:
            message += f"\n{hint}"
        super().__init__(message)


def _distro_hint() -> str:
    """A package-manager-specific install hint, or a generic one."""
    for manager, family in (("pacman", "arch"), ("apt-get", "debian"),
                            ("dnf", "fedora"), ("zypper", "suse")):
        if shutil.which(manager) or os.path.exists(f"/usr/bin/{manager}"):
            return _DISTRO_HINTS[family]
    return "install the equivalent of plasma-workspace, kconfig and qt6-tools"


def _host_path() -> str:
    """PATH with any directory inside the mounted AppImage removed.

    Our AppRun does not add itself to PATH, so normally this is a no-op and
    exists as a backstop. It matters because the bundle ships a ``python3``: if
    anything ever does put the mount on PATH, ``find_host_python()`` would find
    that one, and the native venv -- which has to be built by the interpreter
    that will run it -- would be created by an interpreter that disappears when
    the AppImage unmounts.
    """
    parts = [p for p in os.environ.get("PATH", "").split(os.pathsep) if p]
    appdir = os.environ.get("APPDIR")
    if appdir:
        appdir = os.path.abspath(appdir)
        parts = [p for p in parts
                 if not (os.path.abspath(p) == appdir
                         or os.path.abspath(p).startswith(appdir + os.sep))]
    return os.pathsep.join(parts)


def _host_env() -> dict[str, str]:
    """The environment to hand to a host command.

    Inside the AppImage this is the session's environment with the bundle's own
    variables removed; outside it, ``os.environ`` unchanged. Dropping
    ``XDG_*_DIRS`` entirely would be wrong -- the desktop session sets them and
    host tools legitimately need them -- so the mount's entries are subtracted
    and the rest is kept.
    """
    env = dict(os.environ)
    if not IN_APPIMAGE:
        return env
    appdir = os.path.abspath(env.get("APPDIR", "")) if env.get("APPDIR") else ""
    for name in _APPIMAGE_ENV_VARS:
        value = env.pop(name, None)
        if value and appdir and name in ("XDG_DATA_DIRS", "XDG_CONFIG_DIRS"):
            kept = [p for p in value.split(os.pathsep)
                    if p and not os.path.abspath(p).startswith(appdir + os.sep)]
            if kept:
                env[name] = os.pathsep.join(kept)
    env["PATH"] = _host_path()
    return env


def _run_cancellable(full_argv: list[str], argv: list[str], *, capture: bool,
                     on_output: Callable[[str], None] | None, timeout: int | None,
                     popen_kwargs: dict) -> subprocess.CompletedProcess:
    """Popen-based run that polls for cancellation and can stream output."""
    deadline = time.monotonic() + timeout if timeout else None

    def poll_interrupts(proc) -> None:
        if cancel_requested():
            _terminate(proc)
            raise Cancelled(f"cancelled while running {argv[0]}")
        if deadline is not None and time.monotonic() > deadline:
            _terminate(proc)
            raise subprocess.TimeoutExpired(argv, timeout)

    if on_output is None:
        kwargs = dict(popen_kwargs)
        if capture:
            kwargs.update(stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          encoding="utf-8", errors="replace")
        proc = _Popen(full_argv, **kwargs)
        while True:
            try:
                out, err = proc.communicate(timeout=0.2)
                break
            except subprocess.TimeoutExpired:
                poll_interrupts(proc)       # retrying communicate() loses no output
        return subprocess.CompletedProcess(argv, proc.returncode, out, err)

    # Streaming: stderr folded into stdout so the caller sees one ordered log.
    proc = _Popen(full_argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **popen_kwargs)
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    fd = proc.stdout.fileno()
    chunks: list[str] = []
    pending = ""
    while True:
        ready, _, _ = select.select([fd], [], [], 0.2)
        if ready:
            data = os.read(fd, 65536)
            if not data:
                break                                   # EOF: child is done
            text = decoder.decode(data)
            chunks.append(text)
            pending += text
            *lines, pending = pending.replace("\r", "\n").split("\n")
            for line in lines:
                if line.strip():
                    on_output(line)
        poll_interrupts(proc)
    pending += decoder.decode(b"", final=True)
    if pending.strip():
        on_output(pending)
    proc.wait()
    return subprocess.CompletedProcess(argv, proc.returncode, "".join(chunks), "")


def _execute(argv: list[str], *, check: bool, capture: bool, env: dict[str, str] | None,
            cwd: str | None, timeout: int | None,
            on_output: Callable[[str], None] | None) -> subprocess.CompletedProcess:
    """The actual spawn, with argv[0] exactly as given -- no PATH resolution.

    Split out from ``run()`` so ``which()`` can call this directly: ``run()``
    resolves argv[0] via ``which()`` before getting here, and if ``which()``
    went through ``run()`` too it would call itself while still resolving and
    never return.
    """
    kwargs: dict = {}
    if capture:
        kwargs.update(capture_output=True, text=True, encoding="utf-8", errors="replace")
    if timeout:
        kwargs["timeout"] = timeout
    # Always explicit, never inherited: this is what keeps the AppImage's own
    # variables out of the host command's environment.
    child_env = {**_host_env(), **(env or {})}
    kwargs["env"] = child_env
    popen_kwargs: dict = {"env": child_env}
    if cwd:
        kwargs["cwd"] = popen_kwargs["cwd"] = cwd

    try:
        if on_output is not None or _cancel_event is not None:
            proc = _run_cancellable(argv, argv, capture=capture, on_output=on_output,
                                    timeout=timeout, popen_kwargs=popen_kwargs)
        else:
            proc = _subprocess_run(argv, **kwargs)
    except FileNotFoundError:
        # Surface a missing binary as exit 127 rather than an exception, so a
        # caller using check=False can decide for itself -- the same contract a
        # shell gives, and what `command -v`-style probes expect.
        stderr = f"{argv[0]}: not found"
        proc = subprocess.CompletedProcess(argv, 127, "" if capture else None,
                                           stderr if capture else None)
    if check and proc.returncode != 0:
        raise HostCommandError(argv, proc.returncode,
                               getattr(proc, "stderr", "") or "", _distro_hint())
    return proc


def run(argv: Iterable[str], *, check: bool = False, capture: bool = False,
        env: dict[str, str] | None = None, cwd: str | None = None,
        timeout: int | None = None,
        on_output: Callable[[str], None] | None = None) -> subprocess.CompletedProcess:
    """Run an external command.

    Shaped like ``subprocess.run`` so call sites migrate mechanically:
    ``capture=True`` is ``capture_output=True, text=True`` (plus explicit UTF-8
    decoding), and ``capture=False`` inherits our stdio the way a bare
    ``subprocess.run`` does. ``on_output`` streams the command's output a line
    at a time (stderr merged in); it and a registered cancel event both switch
    to a polling implementation so the command can be interrupted.

    A non-absolute argv[0] is resolved through ``which()`` first. Without
    this, a tool installed somewhere real but off the session's PATH (a
    Homebrew or Nix install is the common case) would pass every preflight
    check yet fail to actually run: ``which()`` searches supplemental
    locations, but the process itself is still exec'd by the plain name,
    which only the session PATH resolves. If ``which()`` finds nothing,
    argv[0] is left as-is and this fails exactly as it did before -- a
    normal "not found".
    """
    argv = [str(a) for a in argv]
    if argv and not os.path.isabs(argv[0]):
        resolved = which(argv[0])
        if resolved:
            argv = [resolved, *argv[1:]]
    return _execute(argv, check=check, capture=capture, env=env, cwd=cwd, timeout=timeout,
                    on_output=on_output)


_which_cache: dict[str, str | None] = {}

# User-space package manager bin dirs that genuinely exist on a machine but
# routinely aren't on the desktop session's PATH -- a login session's PATH is
# set once at login (by PAM/systemd/the display manager), while these get
# added to a *shell's* PATH by profile scripts the session never runs. A tool
# installed this way is real and runnable; it is just invisible to the normal
# lookup, which is what this app actually needs to know. Checked only as a
# fallback appended after the real PATH, so it can never shadow a system copy.
_SUPPLEMENTAL_BIN_DIRS = (
    "/home/linuxbrew/.linuxbrew/bin",              # Homebrew on Linux (this is its one fixed path)
    os.path.expanduser("~/.linuxbrew/bin"),        # Homebrew on Linux, per-user install
    os.path.expanduser("~/.nix-profile/bin"),      # Nix, per-user profile
    "/nix/var/nix/profiles/default/bin",           # Nix, system profile
    os.path.expanduser("~/.local/bin"),
)


def _supplemental_path() -> str:
    return os.pathsep.join(d for d in _SUPPLEMENTAL_BIN_DIRS if os.path.isdir(d))


def which(name: str) -> str | None:
    """Locate an executable on the *host*.

    Searches the session PATH first, in order, then the supplemental dirs above
    as a fallback -- so a duplicate in a supplemental dir can never shadow a
    real system copy. The AppImage's own directories are subtracted from PATH
    (see ``_host_path``), so this can never return the bundle's python3 or any
    other bundled tool that shadows a host one.
    """
    # Our archive utilities are self-contained and always take precedence.
    # Resolve before the PATH cache so installing a bundle also replaces a
    # previously cached miss. No global PATH/library changes are necessary.
    bundled = bundled_tools.executable(name)
    if bundled:
        return bundled
    if name in _which_cache:
        return _which_cache[name]
    extra = _supplemental_path()
    path = _host_path() + (os.pathsep + extra if extra else "")
    found = shutil.which(name, path=path)
    _which_cache[name] = found
    return found


def have(name: str) -> bool:
    return which(name) is not None


def first_available(*names: str) -> str | None:
    """The first of `names` present on the host, e.g. qdbus6 then qdbus."""
    return next((name for name in names if have(name)), None)


def resolve(*names: str) -> str:
    """The name to actually invoke for a tool that has several spellings.

    Falls back to the first name so a missing tool surfaces as a clear
    "not found" from run() rather than silently invoking a different program.
    Results are cached by which(), so this is cheap to call per site.
    """
    return first_available(*names) or names[0]


# --- filesystem -----------------------------------------------------------------
#
# These used to exist because a sandbox could only see its own filesystem, so a
# path outside $HOME had to be read by asking the host to do it. An AppImage has
# no such boundary -- it reads and writes the real filesystem directly -- so
# they are now thin wrappers over the obvious local operation.
#
# They are kept as named functions rather than pushed out to the call sites for
# two reasons: they document which paths are expected to be *the host's* rather
# than the app's own (panel_layout walks system directory entries per launcher
# URL, where the distinction is easy to lose), and they give one place to add
# the host indirection back if the packaging ever becomes sandboxed again.


def read_text(path: str) -> str | None:
    """Read a text file, or None if it is missing or unreadable."""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError:
        return None


def listdir(path: str) -> list[str]:
    """Entry names in a directory, or [] if it does not exist."""
    try:
        return sorted(os.listdir(path))
    except OSError:
        return []


def exists(path: str) -> bool:
    return os.path.exists(path)


def copy_tree(src: str, dst: str) -> None:
    """Copy a directory tree.

    ``-T`` treats `dst` as the destination itself rather than as a parent, so
    this behaves like ``shutil.copytree(..., dirs_exist_ok=True)`` and does not
    depend on whether `dst` already exists. Unlike copytree, ``cp`` will not
    create a missing *parent* of `dst`; it is made here.
    """
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    proc = run(["cp", "-aT", "--", src, dst], capture=True)
    if proc.returncode != 0:
        raise HostCommandError(["cp", "-aT", src, dst], proc.returncode,
                               proc.stderr or "", f"could not copy {src} to {dst}")


def show_environment(name: str) -> str | None:
    """A variable from the *host's* systemd user environment.

    ``os.environ`` is the wrong source even outside a sandbox, because the
    AppImage's own variables are in there: reading one and pushing the result
    into the host's systemd environment would write the mount's paths into a
    setting the rest of the session then inherits. QML2_IMPORT_PATH in
    widget_style is exactly this case.
    """
    proc = run(["systemctl", "--user", "show-environment"], capture=True)
    if proc.returncode != 0:
        return None
    for line in proc.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep and key == name:
            return value
    return None


def _install_subprocess_guard() -> None:
    """Make a bare subprocess call fail loudly while running from the AppImage.

    Not because the command would be *unable* to run -- it is not sandboxed --
    but because it would inherit the bundle's environment and PATH, so a bare
    ``subprocess.run(["python3", ...])`` finds the bundle's interpreter and a
    bare call to a Qt-linked host tool can load our Qt. Both fail in ways that
    look like success. Set ``XP2PLASMA_ALLOW_SUBPROCESS=1`` to disable while
    debugging.
    """
    if not IN_APPIMAGE or os.environ.get("XP2PLASMA_ALLOW_SUBPROCESS"):
        return

    def _blocked(name):
        def guard(*_args, **_kwargs):
            raise RuntimeError(
                f"subprocess.{name}() was called directly while running from the "
                "AppImage.\nEvery external command must go through hostexec.run() "
                "(or copy_tree/read_text/listdir): a direct call inherits the "
                "bundle's PATH and library paths, so it may run the bundle's own "
                "python3 or load the bundle's Qt into a host program.\n"
                "Set XP2PLASMA_ALLOW_SUBPROCESS=1 to bypass while debugging."
            )
        return guard

    # Not Popen: subprocess.run is implemented on top of it, so blocking it
    # would break this module's own calls too. These four are what a call site
    # would actually reach for.
    for name in ("run", "call", "check_call", "check_output"):
        setattr(subprocess, name, _blocked(name))


_install_subprocess_guard()
