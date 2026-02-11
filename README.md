# libnss_synthgrent

A Linux NSS module for synthesizing private groups from passwd NSS records. This is a fairly simple module, and only supports returning synthesized group data.

## Configuration

The module uses the `/etc/synthgrent.conf` file for configuration information. The file is in an INI styled format. Currently, this has the following two configuration settings in the `[synthgrent]` section of the INI file:

- min_uid
- force_lowercase

The `min_uid` ensures that only accounts equal to or greater than the integer set by this option will be processed by this module. Accounts with a UID below the minimum will not have a private group synthesized. It accepts positive integers from 0 up to UINT_MAX, or 4,294,967,295. Defaults to `0` if the confguration file is missing, disabling this feature.

The `force_lowercase` boolean option will down-case the name of the group if set. Accepted boolean values: `true`, `yes`, `1` (case-insensitive) for true; anything else is false. Defaults to `false` if the key is absent or the config file is missing.

## How it Works

If the module is added to the group section of `/etc/nsswitch.conf`, the module is loaded when a user's group information is requested by the GNU C runtime using the standard `getgrent` C routines.

The module then uses the account's UID and username to generate a synthetic group entry for the account so private groups can be implemented in environments where adding hundreds or thousands of groups could be resource prohibitive or operationally difficult.

The record as returned by the `getent` command looks something like this, provided the account name is `jsmith` and that person's account has UID `1024`:

```
jsmith:x:1024:
```

The membership to this synthetic group is managed by the primary group of the user's passwd record. In this case, `jsmith` has the following for their passwd record:

```
jsmith:x:1024:1024:J. Smith,,,,:/home/jsmith:/bin/bash
```

This ensures that additional users cannot be added to it, making it a fully private group.

## DISCLAIMER

This code was created with some assistance from Claude Opus 4.6. All code has been reviewed and tested to validate that it actually does what it claims. While it works in the test environment it was built in, it may have bugs or other unintended behaviours.
