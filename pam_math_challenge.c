/*
 * pam_math_challenge.c — PAM-модуль: математический челлендж как второй
 * фактор входа с блокировкой учётной записи при исчерпании попыток.
 *
 * Целевая система: Linux Mint / Ubuntu (LightDM + Slick Greeter).
 *
 * КАК ОБЕСПЕЧИВАЕТСЯ «ГРАФИЧНОСТЬ»:
 *   У модуля нет собственного GUI. Он вызывает функцию обратного вызова
 *   pam_conv, переданную ему через PAM handle. LightDM/Slick Greeter
 *   реализует эту функцию так, что PAM_PROMPT_ECHO_OFF рисуется как
 *   поле ввода прямо на экране входа, а PAM_TEXT_INFO — как
 *   информационный текст. Поэтому модуль работает в LightDM (многошаговые
 *   диалоги) и в tty, и НЕ работает в SDDM/LXDM/Entrance (они не умеют
 *   дополнительные PAM-диалоги). Это ожидаемо.
 *
 * Логика auth:
 *   1. чтение /etc/security/math_challenge.conf (ошибки => PAM_IGNORE, fail-open);
 *   2. exempt_users => PAM_IGNORE;
 *   3. блокировка активна? => сообщение + PAM_AUTH_ERR;
 *   4. генерация примера, до N попыток диалог;
 *   5. успех => сброс неудач, PAM_SUCCESS/IGNORE;
 *   6. все попытки исчерпаны => lock_until=now+cooldown, PAM_AUTH_ERR.
 *
 * Состояние: /run/math_challenge/<user> (сбрасывается ребутом, root-only).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <syslog.h>
#include <sys/types.h>

/* Linux-PAM. На Debian/Ubuntu/Mint заголовки в /usr/include/security
 * (пакет libpam0g-dev), особых путей не требуется. */
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_ext.h>

#include <security/_pam_macros.h>

#define PAM_SM_AUTH

#define DEFAULT_CONF_PATH  "/etc/security/math_challenge.conf"
#define DEFAULT_STATE_DIR  "/run/math_challenge"
#define DEFAULT_ATTEMPTS   3
#define DEFAULT_COOLDOWN   300
#define MAX_ATTEMPTS       100
#define MAX_COOLDOWN       86400
#define EXEMPT_MAX         64
#define NAME_LEN           256

typedef struct {
    int   difficulty;      /* 0 easy, 1 medium, 2 hard */
    int   attempts;
    int   cooldown;
    int   ignore_flag;     /* 1 => PAM_IGNORE вместо PAM_SUCCESS */
    char *state_dir;
    char  exempt[EXEMPT_MAX][NAME_LEN];
    int   exempt_n;
} cfg_t;

typedef struct {
    long fails;
    long lock_until;       /* epoch, 0 = не заблокирован */
} state_t;

/* ---------------- утилиты ---------------- */

static void secure_zero(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

static void str_rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

/* Строгий разбор целого: [+-]?[0-9]+, без мусора. */
static int parse_long_strict(const char *s, long *out)
{
    const char *p = s;
    long v = 0;
    int neg = 0, digit = 0;

    if (!s) return -1;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    if (!isdigit((unsigned char)*p)) return -1;
    while (*p && isdigit((unsigned char)*p)) {
        if (v > (LONG_MAX - 9) / 10) return -1;
        v = v * 10 + (*p - '0');
        p++;
        digit = 1;
    }
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '\0' || !digit) return -1;
    *out = neg ? -v : v;
    return 0;
}

/* ---------------- конфиг ---------------- */

static void cfg_defaults(cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->difficulty = 0;
    c->attempts   = DEFAULT_ATTEMPTS;
    c->cooldown   = DEFAULT_COOLDOWN;
    c->ignore_flag = 0;
    c->state_dir  = strdup(DEFAULT_STATE_DIR);
    if (!c->state_dir) abort();  /* из-под root при загрузке malloc не подводит */
}

static void cfg_free(cfg_t *c)
{
    if (c->state_dir) {
        free(c->state_dir);
        c->state_dir = NULL;
    }
}

/* Возвращает 0 при полном успехе, -1 при частичном (fail-open всё равно
 * применяется: конфиг частично битый не должен запирать вход). */
static int load_conf(const char *path, cfg_t *c)
{
    FILE *f;
    char line[1024];
    int rc = 0;

    cfg_defaults(c);
    if (!(f = fopen(path, "r"))) return -1;

    while (fgets(line, sizeof(line), f)) {
        char *key, *val, *eq, *nl;
        if (!(nl = strchr(line, '\n')))
            { int ch; while ((ch = fgetc(f)) != '\n' && ch != EOF) ; }
        else *nl = '\0';

        key = line;
        while (*key && isspace((unsigned char)*key)) key++;
        if (*key == '\0' || *key == '#') continue;
        eq = strchr(key, '=');
        if (!eq) { rc = -1; continue; }
        *eq = '\0';
        val = eq + 1;
        str_rtrim(key);
        while (*val && isspace((unsigned char)*val)) val++;
        str_rtrim(val);

        if (!strcmp(key, "difficulty")) {
            if (!strcasecmp(val, "easy"))   c->difficulty = 0;
            else if (!strcasecmp(val, "medium")) c->difficulty = 1;
            else if (!strcasecmp(val, "hard"))   c->difficulty = 2;
            else rc = -1;
        } else if (!strcmp(key, "attempts")) {
            long v;
            if (parse_long_strict(val, &v) == 0 && v >= 1 && v <= MAX_ATTEMPTS)
                c->attempts = (int)v;
            else rc = -1;
        } else if (!strcmp(key, "cooldown_seconds")) {
            long v;
            if (parse_long_strict(val, &v) == 0 && v >= 0 && v <= MAX_COOLDOWN)
                c->cooldown = (int)v;
            else rc = -1;
        } else if (!strcmp(key, "ignore_errors")) {
            if (!strcasecmp(val, "true") || !strcmp(val, "1")) c->ignore_flag = 1;
            else if (!strcasecmp(val, "false") || !strcmp(val, "0")) c->ignore_flag = 0;
            else rc = -1;
        } else if (!strcmp(key, "state_dir")) {
            char *n = strdup(val);
            if (!n) { rc = -1; continue; }
            free(c->state_dir);
            c->state_dir = n;
        } else if (!strcmp(key, "exempt_users")) {
            char *tok = val;
            while (*tok && c->exempt_n < EXEMPT_MAX) {
                char *comma = strchr(tok, ',');
                size_t len = comma ? (size_t)(comma - tok) : strlen(tok);
                while (len && isspace((unsigned char)*tok)) { tok++; len--; }
                str_rtrim(tok);
                len = strlen(tok);
                if (len == 0) { if (!comma) break; tok = comma + 1; continue; }
                if (len > NAME_LEN - 1) { rc = -1; break; }
                memcpy(c->exempt[c->exempt_n], tok, len + 1);
                c->exempt_n++;
                if (!comma) break;
                tok = comma + 1;
            }
            if (*tok && c->exempt_n >= EXEMPT_MAX) rc = -1;
        } else {
            rc = -1;   /* неизвестная директива -> fail-open при ошибках в ключе */
        }
    }
    fclose(f);
    if (!c->state_dir) { rc = -1; c->state_dir = strdup(DEFAULT_STATE_DIR); }
    return rc;
}

/* ---------------- состояние ---------------- */

#define state_dir_mode 01733

static int ensure_state_dir(const char *dir)
{
    char tmp[1024];
    char *p;
    struct stat st;

    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    /* 1733: sticky+wx — каждый пользователь создаёт ТОЛЬКО СВОЙ файл,
     * читать/списавать чужие нельзя, удалять чужие через sticky нельзя. */
    if (mkdir(tmp, 01733) && errno != EEXIST) return -1;
    return 0;
}

static void state_path(const char *dir, const char *user, char *out, size_t n)
{
    snprintf(out, n, "%s/%s", dir, user);
}

/* Атомарная запись: tmp -> rename(). Ошибка записи — не критична для
 * процесса входа, только для работы блокировки (лог warn). */
static int save_state(pam_handle_t *pamh, const char *dir, const char *user,
                      const state_t *s)
{
    char path[2048], tmp[2100];
    int fd;
    char buf[64];

    if (ensure_state_dir(dir)) {
        pam_syslog(pamh, LOG_WARNING, "cannot create state_dir %s", dir);
        return -1;
    }
    state_path(dir, user, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        pam_syslog(pamh, LOG_WARNING, "open %s: %m", tmp);
        return -1;
    }
    snprintf(buf, sizeof(buf), "fails=%ld\nlock_until=%ld\n", s->fails, s->lock_until);
    if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf) || fsync(fd)) {
        pam_syslog(pamh, LOG_WARNING, "write %s: %m", tmp);
        close(fd); unlink(tmp);
        return -1;
    }
    chmod(tmp, 0600);
    close(fd);
    if (rename(tmp, path)) {
        pam_syslog(pamh, LOG_WARNING, "rename %s: %m", tmp);
        unlink(tmp);
        return -1;
    }
    return 0;
}

static void load_state(const char *dir, const char *user, state_t *s)
{
    char path[2048];
    FILE *f;
    char line[128];

    memset(s, 0, sizeof(*s));
    state_path(dir, user, path, sizeof(path));
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "fails=", 6))
            s->fails = strtol(line + 6, NULL, 10);
        else if (!strncmp(line, "lock_until=", 11))
            s->lock_until = strtol(line + 11, NULL, 10);
    }
    fclose(f);
    if (s->fails <= 0) s->fails = 0;
    if (s->lock_until < 0) s->lock_until = 0;
}

/* ---------------- PAM-диалог ---------------- */

/* Задать вопрос и получить ответ. Именно этот вызов conv() LightDM
 * оборачивает в GUI-поле ввода. */
static int ask_user(pam_handle_t *pamh, const char *question, char **answer_out)
{
    struct pam_message msg;
    const struct pam_message *msgp = &msg;
    struct pam_response *resp = NULL;
    struct pam_conv *conv;
    int ret;

    msg.msg_style = PAM_PROMPT_ECHO_OFF;
    msg.msg       = (char *)question;

    if (pam_get_item(pamh, PAM_CONV, (const void **)&conv) != PAM_SUCCESS
        || !conv || !conv->conv)
        return -1;

    ret = conv->conv(1, &msgp, &resp, conv->appdata_ptr);
    if (ret != PAM_SUCCESS) {
        /* Согласно spec, при ошибке conv обязан сам очистить свои response'ы;
         * на всякий случай чистим, если вернул. */
        if (resp) {
            if (resp->resp) { secure_zero(resp->resp, strlen(resp->resp)); free(resp->resp); }
            free(resp);
        }
        return -1;
    }
    if (!resp || !resp->resp) {
        if (resp) free(resp);
        return -1;
    }
    *answer_out = resp->resp;   /* владение переходит; secure_zero обязателен */
    free(resp);
    return 0;
}

/* Информационное сообщение: LightDM показывает текст статус-баром/label. */
static void info_msg(pam_handle_t *pamh, const char *text)
{
    struct pam_message msg;
    const struct pam_message *msgp = &msg;
    struct pam_response *resp = NULL;
    struct pam_conv *conv;

    msg.msg_style = PAM_TEXT_INFO;
    msg.msg       = (char *)text;
    if (pam_get_item(pamh, PAM_CONV, (const void **)&conv) != PAM_SUCCESS
        || !conv || !conv->conv) return;
    conv->conv(1, &msgp, &resp, conv->appdata_ptr);
    if (resp) { if (resp->resp) free(resp->resp); free(resp); }
}

/* ---------------- генерация примеров ---------------- */

static int rand_in(int lo, int hi)
{
    return lo + (int)(random() % (unsigned long)(hi - lo + 1));
}

static void seed_rng(void)
{
    unsigned int seed;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, &seed, sizeof(seed)) != sizeof(seed))
            seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        close(fd);
    } else {
        seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    }
    srandom(seed);
}

/* Easy: a op b в пределах 20, целые. */
static void gen_easy(char *buf, size_t n, int *ans)
{
    int a = rand_in(2, 10);
    int b = rand_in(2, 10);
    if (a + b <= 20 && (rand() & 1)) {
        snprintf(buf, n, "Сколько будет %d + %d?", a, b);
        *ans = a + b;
    } else {
        if (b > a) { int t = a; a = b; b = t; }
        snprintf(buf, n, "Сколько будет %d - %d?", a, b);
        *ans = a - b;
    }
}

/* Medium: умножение/деление до 12, целые, плюс отрицательные. */
static void gen_medium(char *buf, size_t n, int *ans)
{
    int a = rand_in(2, 12), b = rand_in(2, 12);

    if (rand() & 1) {
        if (rand() & 1) {           /* умножение с возможным минусом */
            int sign = (rand() & 1) ? -1 : 1;
            int c = a * sign;
            snprintf(buf, n, "Сколько будет %d × %d?", c, b);
            *ans = c * b;
        } else {                    /* деление без остатка */
            snprintf(buf, n, "Сколько будет %d ÷ %d?", a * b, b);
            *ans = a;
        }
    } else {
        snprintf(buf, n, "Сколько будет %d × %d?", a, b);
        *ans = a * b;
    }
}

/* Hard: скобки, два-три операнда, целые результаты. */
static void gen_hard(char *buf, size_t n, int *ans)
{
    int a = rand_in(2, 12), b = rand_in(2, 12), c = rand_in(2, 12);

    switch (rand() % 3) {
    case 0:
        snprintf(buf, n, "Сколько будет (%d + %d) × %d?", a, b, c);
        *ans = (a + b) * c;
        break;
    case 1: {
        while (c >= a * b) c = rand_in(2, 12);
        snprintf(buf, n, "Сколько будет %d × %d − %d?", a, b, c);
        *ans = a * b - c;
        break;
    }
    default: {
        int d = rand_in(0, 10);
        int e = rand_in(0, d);      /* d - e >= 0, без остатка/отрицательных */
        snprintf(buf, n, "Сколько будет %d + (%d − %d)?", a, d, e);
        *ans = a + d - e;
        break;
    }
    }
}

static void gen_question(const cfg_t *c, char *buf, size_t n, int *ans)
{
    switch (c->difficulty) {
    case 0: gen_easy(buf, n, ans); break;
    case 1: gen_medium(buf, n, ans); break;
    default: gen_hard(buf, n, ans); break;
    }
}

/* ---------------- основной auth ---------------- */

static int user_is_exempt(const cfg_t *c, const char *user)
{
    int i;
    for (i = 0; i < c->exempt_n; i++)
        if (!strcmp(c->exempt[i], user)) return 1;
    return 0;
}

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags,
                                   int argc, const char **argv)
{
    (void)flags;
    cfg_t cfg;
    state_t st;
    const char *user = NULL;
    const char *conf_path = DEFAULT_CONF_PATH;
    char question[256];
    long now;
    int i, final_status;

    for (i = 0; i < argc; i++)
        if (!strncmp(argv[i], "conf=", 5)) conf_path = argv[i] + 5;

    seed_rng();

    if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS || !user || !*user) {
        pam_syslog(pamh, LOG_ERR, "cannot determine user");
        return PAM_SERVICE_ERR;
    }

    /* Flags пока не используем, но проверим PAM_SILENT на будущее. */
    if (strchr(user, '/')) {   /* защита от path traversal в имени */
        pam_syslog(pamh, LOG_ERR, "suspicious user name '%s'", user);
        return PAM_SERVICE_ERR;
    }

    if (load_conf(conf_path, &cfg) != 0) {
        /* Fail-open: конфиг отсутствует/частично битый — вход не блокируем. */
        pam_syslog(pamh, LOG_ERR, "conf %s missing/invalid, fail-open",
                   conf_path);
        cfg_free(&cfg);
        return PAM_IGNORE;
    }

    if (user_is_exempt(&cfg, user)) {
        cfg_free(&cfg);
        return PAM_IGNORE;
    }

    load_state(cfg.state_dir, user, &st);
    now = time(NULL);

    /* Активная блокировка? */
    if (st.lock_until > now) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "Учётная запись заблокирована. Попробуйте позже (осталось ~%ld с)",
                 st.lock_until - now);
        info_msg(pamh, msg);
        pam_syslog(pamh, LOG_NOTICE, "user %s locked for ~%ld s", user,
                   st.lock_until - now);
        cfg_free(&cfg);
        return PAM_AUTH_ERR;
    }

    /* Блокировка истекла: сброс. */
    if (st.lock_until && st.lock_until <= now) {
        st.fails = 0;
        st.lock_until = 0;
        save_state(pamh, cfg.state_dir, user, &st);
    }

    final_status = PAM_AUTH_ERR;
    for (i = 0; i < cfg.attempts; i++) {
        int ans = 0;
        char *answer = NULL;
        long got = 0;
        int ok;

        /* Каждый раз новый пример (защита от replay). */
        gen_question(&cfg, question, sizeof(question), &ans);

        if (ask_user(pamh, question, &answer) != 0) {
            /* Greeter не смог показать диалог (crash/timeout) — это не
             * неверно введённый ответ, штраф не начисляем. */
            pam_syslog(pamh, LOG_ERR, "conv failed for %s (attempt %d/%d)",
                       user, i + 1, cfg.attempts);
            cfg_free(&cfg);
            return PAM_CONV_ERR;
        }

        int numeric = (parse_long_strict(answer, &got) == 0);
        ok = numeric && (got == ans);
        /* Диагностика: НЕ логируем сам ответ, только его форму и длину. */
        pam_syslog(pamh, LOG_INFO,
                   "attempt %d/%d: answer len=%zu numeric=%s",
                   i + 1, cfg.attempts, strlen(answer), numeric ? "yes" : "no");
        secure_zero(answer, strlen(answer));
        free(answer);
        /* q: вопрос и ans живут только на стеке этой итерации. */

        if (ok) {
            st.fails = 0;
            st.lock_until = 0;
            if (save_state(pamh, cfg.state_dir, user, &st))
                pam_syslog(pamh, LOG_WARNING,
                           "cannot persist reset state for %s", user);
            final_status = cfg.ignore_flag ? PAM_IGNORE : PAM_SUCCESS;
            cfg_free(&cfg);
            return final_status;
        }

        if (i < cfg.attempts - 1)
            info_msg(pamh, "Неверный ответ. Попробуйте ещё раз.");
    }

    /* Исчерпаны попытки -> блокировка. */
    st.fails++;
    if (cfg.cooldown > 0)
        st.lock_until = time(NULL) + cfg.cooldown;
    info_msg(pamh, "Неверный ответ. Учётная запись заблокирована.");
    if (save_state(pamh, cfg.state_dir, user, &st))
        pam_syslog(pamh, LOG_WARNING, "cannot persist lock for %s", user);
    pam_syslog(pamh, LOG_NOTICE, "user %s failed math challenge, cooldown=%d",
               user, cfg.cooldown);

    cfg_free(&cfg);
    return PAM_AUTH_ERR;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags,
                              int argc, const char **argv)
{
    (void)pamh; (void)flags; (void)argc; (void)argv;
    return PAM_CRED_UNAVAIL;   /* учётные данные не наш профиль */
}

PAM_EXTERN int pam_sm_chauthtok(pam_handle_t *pamh, int flags,
                                int argc, const char **argv)
{
    (void)pamh; (void)flags; (void)argc; (void)argv;
    return PAM_SERVICE_ERR;
}

PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh, int flags,
                                   int argc, const char **argv)
{
    (void)pamh; (void)flags; (void)argc; (void)argv;
    return PAM_SERVICE_ERR;
}

PAM_EXTERN int pam_sm_close_session(pam_handle_t *pamh, int flags,
                                    int argc, const char **argv)
{
    (void)pamh; (void)flags; (void)argc; (void)argv;
    return PAM_SERVICE_ERR;
}

#ifdef PAM_STATIC
struct pam_module pam_math_challenge_modstruct = {
    "pam_math_challenge",
    pam_sm_authenticate,
    pam_sm_setcred,
    pam_sm_chauthtok,
    pam_sm_open_session,
    pam_sm_close_session,
    NULL,
};
#endif
