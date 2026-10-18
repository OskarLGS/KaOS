#ifndef USERS_H
#define USERS_H

#include <stdint.h>

// User accounts, stored in disk/etc/passwd as lines
//   name:uid:salt:hash
// hash = hex(SHA-256 iterated over salt + password). Names are 1-8 chars
// of [a-z0-9_] (so a FAT 8.3 alias can never differ from the name).

#define UID_ROOT   0
#define UID_NOBODY 65535          // Before anyone logs in
#define USER_NAME_MAX 9           // 8 chars + NUL
#define MAX_USERS  16
#define PASSWD_PATH "disk/etc/passwd"

typedef struct {
    char     name[USER_NAME_MAX];
    uint32_t uid;
} user_t;

// Read the accounts. Returns 1 if a passwd file exists, 0 on first boot
// (no file yet), -1 if there is no disk at all (accounts disabled).
int  users_init();
int  users_disabled();

// 1 if `name` is an acceptable user name
int  users_valid_name(const char* name);
// Look a user up; 1 if found
int  users_find(const char* name, user_t* out);
int  users_find_uid(uint32_t uid, user_t* out);
int  users_list(user_t* out, int max);

// 1 if `password` is right for `name`
int  users_check(const char* name, const char* password);
// Create an account (uid assigned) with its home directory. 0 on success.
int  users_add(const char* name, const char* password);
// Change a password. 0 on success.
int  users_set_password(const char* name, const char* password);
// First boot: create disk/etc, disk/home, disk/tmp and the root account.
int  users_setup_root(const char* password);

// ─── Permissions (path based) ─────────────────────────────────────────────────
// Root may do anything. Others may write only below disk/home/<name>/ and
// disk/tmp/, and read everything except the passwd file. Paths are
// normalised first (".", "..", repeated slashes, case), so detours like
// disk/home/me/../../etc/passwd are caught.
int  perm_read(uint32_t uid, const char* path);
int  perm_write(uint32_t uid, const char* path);

// Lower-case, '/'-separated, no "." / ".." / empty parts. 0 if it would
// climb above the root.
int  path_normalize(const char* in, char* out, int max);

#endif
