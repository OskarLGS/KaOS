#include "../shell.h"
#include "../screen.h"
#include "../util.h"
#include "../task.h"
#include "../users.h"

// Two words max: "su oskar", "passwd", ...
static char* first_word(char* s) {
    while (*s == ' ') s++;
    char* e = s;
    while (*e && *e != ' ') e++;
    *e = '\0';
    return s;
}

static void say(const char* a, const char* b, const char* c) {
    char line[96];
    strcpy(line, (char*)a);
    strcpy(line + strlen(line), (char*)b);
    strcpy(line + strlen(line), (char*)c);
    print_string(line);
}

static void cmd_whoami(char* args) {
    (void)args;
    char uid[12];
    int_to_ascii((int)current_uid(), uid);
    say(shell_user(), " (uid ", uid);
    print_string(")\n");
}

static void cmd_users(char* args) {
    (void)args;
    user_t list[MAX_USERS];
    int n = users_list(list, MAX_USERS);
    if (users_disabled()) { print_string("Accounts are off (no disk).\n"); return; }
    for (int i = 0; i < n; i++) {
        char uid[12];
        int_to_ascii((int)list[i].uid, uid);
        char line[48];
        strcpy(line, list[i].name);
        while (strlen(line) < 10) append(line, ' ');
        strcpy(line + strlen(line), uid);
        append(line, '\n');
        print_string(line);
    }
}

static void cmd_logout(char* args) {
    (void)args;
    if (users_disabled()) { print_string("Accounts are off (no disk).\n"); return; }
    shell_login_prompt();
}

// ─── su ───────────────────────────────────────────────────────────────────────

static char target[USER_NAME_MAX];

static void su_password(char* pw) {
    user_t u;
    if (users_check(target, pw) && users_find(target, &u)) {
        shell_set_user(u.name, u.uid);
    } else {
        print_string("su: authentication failure\n");
    }
    shell_handler("");      // Prompt (as the new user if it worked)
}

static void cmd_su(char* args) {
    char* name = first_word(args);
    if (!*name) name = "root";
    user_t u;
    if (users_disabled() || !users_find(name, &u)) {
        print_string("su: no such user\n");
        return;
    }
    strcpy(target, name);
    if (current_uid() == UID_ROOT) {    // Root needs no password
        shell_set_user(u.name, u.uid);
        return;
    }
    shell_ask("Password: ", 1, su_password);
}

// ─── passwd / useradd (password asked twice) ──────────────────────────────────

static char first_pw[SHELL_LINE_MAX];
static int  adding = 0;            // useradd (1) or passwd (0)

static void new_password_again(char* pw) {
    int same = strcmp(pw, first_pw) == 0;
    memory_set((uint8_t*)first_pw, 0, sizeof(first_pw));
    if (!same) {
        print_string("Passwords differ; nothing changed.\n");
    } else if (adding) {
        if (users_add(target, pw) == 0) {
            say("Created ", target, ", home disk/home/");
            say(target, "\n", "");
        } else {
            print_string("Could not add the user.\n");
        }
    } else {
        print_string(users_set_password(target, pw) == 0 ? "Password changed.\n"
                                                         : "Could not save the password.\n");
    }
    shell_handler("");
}

static void new_password(char* pw) {
    if (strlen(pw) < 4) {
        print_string("At least 4 characters; nothing changed.\n");
        shell_handler("");
        return;
    }
    strcpy(first_pw, pw);
    shell_ask("Retype it: ", 1, new_password_again);
}

static void old_password(char* pw) {
    if (!users_check(target, pw)) {
        print_string("passwd: wrong password\n");
        shell_handler("");
        return;
    }
    shell_ask("New password: ", 1, new_password);
}

static void cmd_passwd(char* args) {
    char* name = first_word(args);
    if (!*name) name = (char*)shell_user();
    if (users_disabled() || !users_find(name, &(user_t){0})) {
        print_string("passwd: no such user\n");
        return;
    }
    if (current_uid() != UID_ROOT && strcmp(name, (char*)shell_user()) != 0) {
        print_string("passwd: only root can change other users' passwords\n");
        return;
    }
    strcpy(target, name);
    adding = 0;
    if (current_uid() == UID_ROOT) shell_ask("New password: ", 1, new_password);
    else shell_ask("Current password: ", 1, old_password);
}

static void cmd_useradd(char* args) {
    char* name = first_word(args);
    if (current_uid() != UID_ROOT) { print_string("useradd: only root can add users\n"); return; }
    if (users_disabled()) { print_string("Accounts are off (no disk).\n"); return; }
    if (!users_valid_name(name)) {
        print_string("useradd: name must be 1-8 chars of a-z, 0-9, _\n");
        return;
    }
    if (users_find(name, &(user_t){0})) { print_string("useradd: user exists\n"); return; }
    strcpy(target, name);
    adding = 1;
    shell_ask("Password for the new user: ", 1, new_password);
}

void init_cmd_users() {
    register_command("whoami", "Show the current user", cmd_whoami);
    register_command("users", "List user accounts", cmd_users);
    register_command("su", "Become another user (su [name])", cmd_su);
    register_command("passwd", "Change a password (passwd [name])", cmd_passwd);
    register_command("useradd", "Add a user (root)", cmd_useradd);
    register_command("logout", "End the session", cmd_logout);
}
