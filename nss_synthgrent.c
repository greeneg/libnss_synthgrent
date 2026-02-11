/*
 * libnss_synthgrent - NSS module for synthesizing private groups
 *
 * Copyright (C) 2026, YggdrasilSoft, LLC.
 * Licensed under the GNU Lesser General Public License v2.1 or later.
 * For licensing details, see the LICENSE file distributed with this software.
 *
 * For each user in the passwd database, this module synthesizes a group
 * entry where the group name matches the username and the GID matches
 * the user's UID. The member list is empty, since the user is implicitly
 * a member via their primary GID in passwd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <nss.h>
#include <pthread.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_PATH "/etc/synthgrent.conf"
#define DEFAULT_MIN_UID 0
#define DEFAULT_FORCE_LOWERCASE 0

static pthread_mutex_t enum_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Configuration state — protected by config_mutex */
static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
static uid_t  config_min_uid         = DEFAULT_MIN_UID;
static int    config_force_lowercase = DEFAULT_FORCE_LOWERCASE;
static time_t config_mtime           = 0;

/*
 * Trim leading whitespace in-place and return pointer to first
 * non-whitespace character.
 */
static char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/*
 * Trim trailing whitespace in-place by writing a NUL terminator.
 */
static void rtrim(char *s) {
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
}

/*
 * Parse a boolean string value.
 * Accepts "true", "yes", "1" as true; anything else as false.
 * Comparison is case-insensitive.
 */
static int parse_bool(const char *val) {
    return (strcasecmp(val, "true") == 0 ||
            strcasecmp(val, "yes") == 0 ||
            strcmp(val, "1") == 0);
}

/*
 * Parse /etc/synthgrent.conf (INI format) and update configuration.
 * Must be called with config_mutex held.
 *
 * Expected format:
 *   [synthgrent]
 *   min_uid = 1000
 *   force_lowercase = true
 */
static void load_config(void) {
    struct stat st;

    if (stat(CONFIG_PATH, &st) != 0) {
        /* File missing or inaccessible — use defaults */
        config_min_uid = DEFAULT_MIN_UID;
        config_force_lowercase = DEFAULT_FORCE_LOWERCASE;
        config_mtime = 0;
        return;
    }

    if (st.st_mtime == config_mtime) {
        return; /* unchanged */
    }

    config_mtime = st.st_mtime;
    config_min_uid = DEFAULT_MIN_UID;
    config_force_lowercase = DEFAULT_FORCE_LOWERCASE;

    FILE *fp = fopen(CONFIG_PATH, "r");
    if (fp == NULL) {
        return;
    }

    char line[256];
    int in_section = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = ltrim(line);

        /* Skip blank lines and comments */
        if (*p == '\0' || *p == '#' || *p == ';') {
            continue;
        }

        /* Section header */
        if (*p == '[') {
            rtrim(p);
            in_section = (strcmp(p, "[synthgrent]") == 0);
            continue;
        }

        if (!in_section) {
            continue;
        }

        /* Look for key = value */
        char *eq = strchr(p, '=');
        if (eq == NULL) {
            continue;
        }

        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        rtrim(key);
        val = ltrim(val);
        rtrim(val);

        if (strcmp(key, "min_uid") == 0) {
            char *endptr;
            unsigned long v = strtoul(val, &endptr, 10);
            if (endptr != val && *endptr == '\0') {
                config_min_uid = (uid_t)v;
            }
        } else if (strcmp(key, "force_lowercase") == 0) {
            config_force_lowercase = parse_bool(val);
        }
    }

    fclose(fp);
}

/*
 * Return the current min_uid, reloading the config file if it has changed.
 */
static uid_t get_min_uid(void) {
    uid_t uid;

    pthread_mutex_lock(&config_mutex);
    load_config();
    uid = config_min_uid;
    pthread_mutex_unlock(&config_mutex);

    return uid;
}

/*
 * Return whether group names should be forced to lowercase.
 */
static int get_force_lowercase(void) {
    int val;

    pthread_mutex_lock(&config_mutex);
    load_config();
    val = config_force_lowercase;
    pthread_mutex_unlock(&config_mutex);

    return val;
}

/*
 * Pack a synthetic group entry into the caller-provided buffer.
 *
 * Buffer layout:
 *   [ group name string (NUL-terminated) ]
 *   [ padding for pointer alignment       ]
 *   [ gr_mem: single NULL pointer          ]
 */
static enum nss_status fill_group(const char *name, gid_t gid, int lowercase,
                                  struct group *grp, char *buf, size_t buflen,
                                  int *errnop) {
    size_t namelen = strlen(name) + 1;

    /* Compute aligned offset for the gr_mem pointer array */
    size_t mem_offset = (namelen + sizeof(char *) - 1) & ~(sizeof(char *) - 1);
    size_t needed = mem_offset + sizeof(char *);

    if (buflen < needed) {
        *errnop = ERANGE;
        return NSS_STATUS_TRYAGAIN;
    }

    memcpy(buf, name, namelen);

    if (lowercase) {
        for (size_t i = 0; buf[i] != '\0'; i++) {
            buf[i] = tolower((unsigned char)buf[i]);
        }
    }

    char **mem = (char **)(buf + mem_offset);
    mem[0] = NULL;

    grp->gr_name = buf;
    grp->gr_passwd = (char *)"x";
    grp->gr_gid = gid;
    grp->gr_mem = mem;

    return NSS_STATUS_SUCCESS;
}

/* Look up a group by name */
enum nss_status _nss_synthgrent_getgrnam_r(const char *name, struct group *grp,
                                           char *buf, size_t buflen,
                                           int *errnop) {
    struct passwd pwbuf;
    struct passwd *result = NULL;
    char tmpbuf[1024];

    int rc = getpwnam_r(name, &pwbuf, tmpbuf, sizeof(tmpbuf), &result);
    if (rc != 0 || result == NULL) {
        return NSS_STATUS_NOTFOUND;
    }

    if (result->pw_uid < get_min_uid()) {
        return NSS_STATUS_NOTFOUND;
    }

    return fill_group(result->pw_name, result->pw_uid, get_force_lowercase(),
                      grp, buf, buflen, errnop);
}

/* Look up a group by GID */
enum nss_status _nss_synthgrent_getgrgid_r(gid_t gid, struct group *grp,
                                           char *buf, size_t buflen,
                                           int *errnop) {
    if (gid < get_min_uid()) {
        return NSS_STATUS_NOTFOUND;
    }

    struct passwd pwbuf;
    struct passwd *result = NULL;
    char tmpbuf[1024];

    int rc = getpwuid_r(gid, &pwbuf, tmpbuf, sizeof(tmpbuf), &result);
    if (rc != 0 || result == NULL) {
        return NSS_STATUS_NOTFOUND;
    }

    return fill_group(result->pw_name, result->pw_uid, get_force_lowercase(),
                      grp, buf, buflen, errnop);
}

/* Begin group enumeration */
enum nss_status _nss_synthgrent_setgrent(int stayopen) {
    (void)stayopen;

    pthread_mutex_lock(&enum_mutex);
    setpwent();
    pthread_mutex_unlock(&enum_mutex);

    return NSS_STATUS_SUCCESS;
}

/* Return the next synthesized group entry */
enum nss_status _nss_synthgrent_getgrent_r(struct group *grp, char *buf,
                                           size_t buflen, int *errnop) {
    struct passwd *pw;
    enum nss_status status;
    uid_t min_uid = get_min_uid();

    pthread_mutex_lock(&enum_mutex);

    /* Skip passwd entries below min_uid */
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_uid >= min_uid) {
            break;
        }
    }

    if (pw == NULL) {
        pthread_mutex_unlock(&enum_mutex);
        return NSS_STATUS_NOTFOUND;
    }

    status = fill_group(pw->pw_name, pw->pw_uid, get_force_lowercase(),
                        grp, buf, buflen, errnop);
    pthread_mutex_unlock(&enum_mutex);

    return status;
}

/* End group enumeration */
enum nss_status _nss_synthgrent_endgrent(void) {
    pthread_mutex_lock(&enum_mutex);
    endpwent();
    pthread_mutex_unlock(&enum_mutex);

    return NSS_STATUS_SUCCESS;
}
