/*
 * pamtest_conv.c — тестовый harness: проходит PAM-аутентификацию сервиса,
 * переданного argv[1], для пользователя argv[2]. В conv все промпты печатаются
 * в stdout (полный текст, включая пример). Ответ на промпт с примером
 * вычисляется тестом сам (парсер простых выражений "a op b"),
 * пароль берётся из переменной окружения TEST_PW и подставляется в формате
 * "пароль:ответ" (combined-режим) либо по отдельности (multi-prompt).
 *
 * Выход: 0 = PAM_SUCCESS, прочее = код ошибки PAM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <security/pam_appl.h>

static const char *PW;

/* UTF-8 aware сравнение с оператором по указателю */
static const char *OP_MUL[] = {"×", "x", NULL};
static const char *OP_DIV[] = {"÷", "/", NULL};
static const char *OP_SUB[] = {"−", "-", NULL};

static int match_op(const char *p, const char **names)
{
    for (int i = 0; names[i]; i++) {
        size_t l = strlen(names[i]);
        if (strncmp(p, names[i], l) == 0) return (int)l;
    }
    return 0;
}

/* Очень простой парсер "Сколько будет A op B?" (UTF-8-операторы) */
static long solve(const char *s)
{
    long a, b;
    const char *p = s;
    int l;

    while (*p && !isdigit((unsigned char)*p)) p++;
    if (!*p) return -999999;
    a = strtol(p, (char **)&p, 10);

    while (*p) {
        if ((l = match_op(p, OP_MUL))) return a * strtol(p + l, 0, 10);
        if ((l = match_op(p, OP_DIV))) { b = strtol(p + l, 0, 10); return b ? a / b : -999999; }
        if ((l = match_op(p, OP_SUB))) return a - strtol(p + l, 0, 10);
        if (*p == '+') return a + strtol(p + 1, 0, 10);
        p++;
    }
    return -999999;
}

static int conv(int n, const struct pam_message **m, struct pam_response **r, void *u)
{
    int i;
    (void)u;
    struct pam_response *resp = calloc((size_t)n, sizeof(*resp));
    if (!resp) return PAM_BUF_ERR;
    for (i = 0; i < n; i++) {
        const struct pam_message *msg = m[i];
        char in[512] = {0};
        printf("PROMPT[%d] style=%d text=\"%.200s\"\n", i, msg->msg_style, msg->msg);
        fflush(stdout);
        /* Пример в combined-режиме: "pw:ответ"; в multi-режиме — просто число. */
        if (strstr(msg->msg, "Пример")) {
            snprintf(in, sizeof(in), "%s:%ld", PW, solve(msg->msg));
        } else if (strstr(msg->msg, "Сколько")) {
            snprintf(in, sizeof(in), "%ld", solve(msg->msg));
        } else if (msg->msg_style == PAM_PROMPT_ECHO_OFF) {
            snprintf(in, sizeof(in), "%s", PW);
        }
        /* Имитация одно-промптового greeter'а, который отвечает пустотой:
         * если задан env EMPTY_ANS — на промпты модуля отвечаем пусто. */
        if (getenv("EMPTY_ANS") && (strstr(msg->msg, "Пример") || strstr(msg->msg, "Сколько")))
            in[0] = '\0';
        printf("REPLY(после вычисления): «%s»\n", in);
        fflush(stdout);
        resp[i].resp = strdup(in);
    }
    *r = resp;
    return PAM_SUCCESS;
}

int main(int argc, char **argv)
{
    pam_handle_t *pamh = NULL;
    struct pam_conv c = { .conv = conv, .appdata_ptr = NULL };
    int ret;

    if (argc < 3) { fprintf(stderr, "use: %s <service> <user>\n", argv[0]); return 2; }
    PW = getenv("TEST_PW");
    if (!PW) { fprintf(stderr, "TEST_PW не задана\n"); return 2; }

    ret = pam_start(argv[1], argv[2], &c, &pamh);
    if (ret != PAM_SUCCESS) { fprintf(stderr, "pam_start: %s\n", pam_strerror(pamh, ret)); return ret; }
    ret = pam_authenticate(pamh, 0);
    printf("RESULT: %d (%s)\n", ret, pam_strerror(pamh, ret));
    pam_end(pamh, ret);
    return ret == PAM_SUCCESS ? 0 : 1;
}
