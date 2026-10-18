# User Accounts and Permissions (kernel/users.c)

## Accounts

`disk/etc/passwd`, one line per user:

```
name:uid:salt:hash
root:0:b08e24a3d9d61372:570d28b1...
```

| Field | |
|-------|-|
| name | 1-8 chars of `a-z 0-9 _` (so the FAT 8.3 alias always equals the name) |
| uid | root = 0, users from 1000; `UID_NOBODY` (65535) before login |
| salt | 16 hex chars from RDTSC + RTC + ticks, hashed |
| hash | SHA-256(salt + password), then 999 x SHA-256(digest + salt), hex |

SHA-256 is `kernel/sha256.c` (checked against the FIPS test vectors).
Password checks compare in constant time and hash even for unknown
names, so neither timing reveals which accounts exist.

## Session

- **First boot** (no passwd file): the shell asks for a root password
  (twice), creates `disk/etc`, `disk/home`, `disk/tmp` and the root
  account, then shows the login prompt. No default passwords.
- **Login** in text mode and in the desktop terminal; passwords echo as
  `*` and never enter the command history. The prompt shows the user:
  `oskar> `, `root# `.
- **No disk**: accounts are off and the shell runs as root.
- The shell's uid is set on login; every task copies the uid of the
  task that creates it, so programs (`exec`) and jobs (`fetch`) run as
  the user who started them.

| Command | |
|---------|-|
| `whoami` | Name and uid |
| `users` | List accounts |
| `su [name]` | Become another user (root by default); root needs no password |
| `passwd [name]` | Change your password (asks the current one); root can change anyone's |
| `useradd <name>` | Root only: create the account and `disk/home/<name>` |
| `logout` | Back to the login prompt (background programs keep running) |

## Permissions (path based)

FAT32 has no owners or modes, so access is decided from the path:

| Who | Read | Write (create, modify, delete, rename) |
|-----|------|----------------------------------------|
| root | everything | everything |
| others | everything except `disk/etc/passwd` | only below `disk/home/<name>/` and `disk/tmp/` |

Paths are normalised first (`.`, `..`, repeated `/`, upper case), so
`disk/home/me/../../etc/passwd` is still the passwd file. Checked in:
the shell's file commands (`cat`, `echo >`, `touch`, `rm`, `mkdir`,
`cp`, `mv`), `exec`, `fetch > file`, the `open` syscall (programs), and
the Files window.

Limits: a user's home directory itself can't be removed or renamed by
them (only what is inside); there are no groups; Files works before
login with nobody's rights (read-only).
