# Fail-Safe Mode #

If more than the expected number of environments is detected during boot, the
system stops booting. This can be a problem if the user wants to boot the
system with a memory stick to update a broken installation.

In order to allow external boot devices with other environment configurations,
the FAILSAFE flag was introduced. If any environment on the boot device has the
ENV_STATUS_FAILSAVE bit set in the `status_flags`, the boot loader will filter
out all found environments which are NOT on the boot device.
