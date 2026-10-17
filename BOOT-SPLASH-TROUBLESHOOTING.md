# Boot splash troubleshooting

## Black screen or flickering during boot on Intel graphics

On some systems, the Intel graphics driver may become available too late for Plymouth to display the boot splash. This can affect both the XP theme and the distribution's bundled Plymouth theme.

This workaround was confirmed on a Dell Latitude E5470 with Intel Skylake graphics, running Kubuntu 26.04 with kernel 7.0.0-38-generic. The laptop was docked with its lid closed and used a VGA display through the dock. Loading `i915` early restored the splash. This observation does not establish a general VGA, dock, or multi-monitor defect.

First check that the kernel command line contains `splash`:

```bash
cat /proc/cmdline
```

If both the XP theme and the distribution's bundled theme fail to display, and the machine uses the Intel `i915` driver, try loading that driver earlier. The following instructions apply to Ubuntu/Kubuntu systems using `initramfs-tools`; they are not instructions for dracut-based distributions or other GPU drivers.

1. Open the early-load module list:

   ```bash
   sudo nano /etc/initramfs-tools/modules
   ```

2. Add `i915` on its own line if it is not already listed. Preserve existing entries.

3. Rebuild the boot image for the running kernel:

   ```bash
   sudo update-initramfs -u -k "$(uname -r)"
   ```

4. If the rebuild succeeds, reboot and check the splash.

To undo this workaround, remove only the `i915` line you added and rebuild the boot image with the same command.

This is an optional, machine-specific troubleshooting step. The transformation pack installer does not automatically add `i915` or alter graphics-driver boot parameters. Forcing the dock output and disabling the laptop panel did not resolve the reported case; those parameters are not required for this workaround.

Reference: [Ubuntu initramfs configuration documentation](https://manpages.ubuntu.com/manpages/resolute/man5/initramfs.conf.5.html).
