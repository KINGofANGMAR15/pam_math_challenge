#!/usr/bin/env bash
#
# install.sh — автоматическая установка/удаление pam_math_challenge
# для Linux Mint / Ubuntu / Debian (LightDM) и Arch Linux / EndeavourOS (LightDM).
#
# Что делает (install):
#   1. собирает модуль (make);
#   2. находит правильный каталог PAM security/;
#   3. копирует .so и конфиг;
#   4. бэкапит /etc/pam.d/lightdm и добавляет auth-строку (idempotent);
#   5. опционально перезапускает lightdm (--restart).
#
# Что делает (--uninstall):
#   * восстанавливает бэкап /etc/pam.d/lightdm (или убирает строку),
#   * удаляет .so и конфиг.
#
# GUI: запускать из терминала. Root-права получаются через sudo, а если
# мы в графической сессии без парольного sudo — через pkexec (появится
# графический диалог запроса пароля — это и есть «GUI-настройка»).

set -euo pipefail

NAME="pam_math_challenge"
SO_FILE="${NAME}.so"
CONF_LOCAL="$(dirname "$0")/math_challenge.conf"
CONF_TARGET="/etc/security/${NAME}.conf"
PAM_FILE="/etc/pam.d/lightdm"
PAM_LINE="auth optional ${NAME}.so"
BACKUP_FILE="/etc/pam.d/lightdm.pammath.bak"

# ---------- цветной вывод ----------
info()  { printf '\033[1;32m[+]\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m[!]\033[0m %s\n' "$*"; }
err()   { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

# ---------- root-эскалация (CLI sudo или GUI pkexec) ----------
ROOT_CMD=""
acquire_root() {
    if [ "$(id -u)" -eq 0 ]; then
        ROOT_CMD=""
    elif command -v sudo >/dev/null 2>&1 && sudo -v 2>/dev/null; then
        ROOT_CMD="sudo"
        info "использую sudo"
    elif command -v pkexec >/dev/null 2>&1; then
        # В графической сессии pkexec покажет диалог пароля (GUI).
        # Потому что pkexec не наследует DISPLAY доступ скрипту, путь
        # должен быть абсолютным. Ожидаем, что запущено из абсолютного пути.
        ROOT_CMD="pkexec env DISPLAY=${DISPLAY:-} SUDO_USER=${SUDO_USER:-}"
        info "использую pkexec (графический запрос пароля)"
    else
        err "Не удалось получить root: нет ни sudo, ни pkexec."
    fi
}
run_root() {
    if [ -z "$ROOT_CMD" ]; then "$@"; else $ROOT_CMD "$@"; fi
}

# ---------- резолв каталога security/ ----------
find_security_dir() {
    for d in /usr/lib/x86_64-linux-gnu/security \
             /usr/lib/security \
             /usr/lib64/security \
             /lib/security; do
        [ -d "$d" ] && { echo "$d"; return 0; }
    done
    return 1
}

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release && echo "$ID"
    else
        echo unknown
    fi
}

# ---------- установка ----------
do_install() {
    local sec_dir distro
    distro="$(detect_distro)"
    info "Дистрибутив: ${distro:-unknown}"

    case "$distro" in
        linuxmint|ubuntu|debian|arch|endeavouros|manjaro|garuda)
            info "дистрибутив поддерживается";;
        *)
            warn "неизвестный/непротестированный дистрибутив '$distro'."; 
            warn "Продолжаем (наличие libpam + lightdm — обязательна).";;
    esac

    command -v make >/dev/null 2>&1 || err "make не установлен."
    command -v gcc  >/dev/null 2>&1 || err "gcc не установлен."
    [ -f /usr/include/security/pam_modules.h ] \
        || err "PAM header'ы не найдены. Arch: 'pacman -S pam'; Debian: 'apt install libpam0g-dev'."

    info "Сборка..."
    make clean >/dev/null
    make >&2

    [ -f "$SO_FILE" ] || err "не смог собрать $SO_FILE"

    sec_dir="$(find_security_dir)" || err "не найден PAM security/ каталог"

    acquire_root

    info "Копирую $SO_FILE -> $sec_dir/"
    run_root install -m 0755 "$SO_FILE" "$sec_dir/$SO_FILE"

    if [ ! -f "$CONF_TARGET" ]; then
        info "Копирую конфиг -> $CONF_TARGET"
        run_root install -D -m 0644 "$CONF_LOCAL" "$CONF_TARGET"
    else
        info "Конфиг $CONF_TARGET уже есть, не трогаю."
    fi

    # ---------- PAM-стек ----------
    if [ ! -f "$PAM_FILE" ]; then
        err "$PAM_FILE не существует — вероятно, LightDM не установлен."
    fi

    if ! run_root grep -qE "^\s*auth\s+.*${NAME}\.so" "$PAM_FILE"; then
        info "Бэкап $PAM_FILE -> $BACKUP_FILE"
        run_root cp -a "$PAM_FILE" "$BACKUP_FILE"

        # добавляем строку в конец auth-секции (последняя auth-строка или конец файла)
        info "Добавляю '$PAM_LINE' в $PAM_FILE"
        run_root tee -a "$PAM_FILE" > /dev/null <<< "$PAM_LINE"
    else
        info "Строка ${NAME}.so уже в $PAM_FILE, ничего не делаю."
    fi

    echo
    info "Установка завершена."
    info "Перезапустите lightdm (см. ниже), например:"
    echo "     sudo systemctl restart lightdm"
    echo "     (или затем graphical.target; важно из tty или с ssh-сессией открытым)."
    info "А просто выйти из сеанса — lightdm прочитает новый стек автоматически."
    info "Как проверить: см. README.md, раздел «Тестирование»."
}

# ---------- удаление ----------
do_uninstall() {
    local sec_dir
    sec_dir="$(find_security_dir)" || true

    acquire_root

    if [ -f "$BACKUP_FILE" ] && run_root grep -qE "^\s*auth\s+.*${NAME}\.so" "$PAM_FILE"; then
        info "Восстанавливаю $PAM_FILE из $BACKUP_FILE"
        run_root cp -a "$BACKUP_FILE" "$PAM_FILE"
        run_root rm -f "$BACKUP_FILE"
    elif run_root grep -qE "^\s*auth\s+.*${NAME}\.so" "$PAM_FILE" 2>/dev/null; then
        info "Убираю строку ${NAME}.so из $PAM_FILE"
        run_root sed -i "/auth.*${NAME}\.so/d" "$PAM_FILE"
    fi

    if [ -n "$sec_dir" ] && [ -f "$sec_dir/$SO_FILE" ]; then
        info "Удаляю $sec_dir/$SO_FILE"
        run_root rm -f "$sec_dir/$SO_FILE"
    fi
    if [ -f "$CONF_TARGET" ]; then
        info "Удаляю $CONF_TARGET"
        run_root rm -f "$CONF_TARGET"
    fi
    info "Удаление завершено. Модуль деактивирован."
}

# ---------- main ----------
case "${1:-install}" in
    install)   do_install ;;
    --uninstall|-r) do_uninstall ;;
    --help|-h)
        echo "Использование: $0 [install|--uninstall|--help]"
        ;;
    *) err "неизвестная опция '$1' (см. $0 --help)";;
esac
