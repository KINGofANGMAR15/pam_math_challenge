# pam_math_challenge

PAM-модуль для Linux, который добавляет **математический челлендж как второй
фактор входа** и умеет **временно блокировать учётную запись** при исчерпании
попыток.

Поддерживаемые окружения:

| Дистрибутив | Display manager | Статус |
|---|---|---|
| Linux Mint, Ubuntu, Debian | LightDM + Slick Greeter | ✅ основная цель |
| Arch Linux, EndeavourOS     | LightDM / Slick Greeter | ✅ поддерживается |
| любые, TTY                  | tty login               | ⚠️ работает (текстовый диалог) |
| GDM                          | GDM                     | ⚠️ работает, не протестировано |
| SDDM, LXDM, Entrance         | эти DM                  | ❌ не поддерживается (нет многошаговых PAM-диалогов) |

«Графичность» подсказывает сам greeter: модуль не рисует собственный GUI,
а передаёт вопрос через `pam_conv`. LightDM/Slick Greeter рисует его как поле
ввода на экране входа, tty — как обычное read-line сообщение.

## Как работает

1. Пользователь вводит пароль (проверяет `pam_unix` / `lightdm-auth`);
2. `pam_math_challenge` вызывается **после** пароля из `/etc/pam.d/lightdm`;
3. greeter показывает пример (Easy / Medium / Hard);
4. верный ответ → `PAM_SUCCESS`/`PAM_IGNORE`, неверный ответ → часть attempts;
   исчерпал попытки → блокировка (`lock_until=now+cooldown`) и отказ входа;
5. при попытке входа в течение `cooldown_seconds` → сразу отказ, без вопроса.

Состояние хранится в `/run/math_challenge/<user>` (root-only, очищается при
ребуте). Блокировку можно сбросить удалением файла или правкой `/etc/pam.d/lightdm`.

## Установка

### Debian / Ubuntu / Linux Mint

```bash
sudo apt install build-essential libpam0g-dev
cd pam_math_challenge
sudo make install && sudo ./install.sh
```

### Arch Linux / EndeavourOS

```bash
sudo pacman -S base-devel pam
cd pam_math_challenge
sudo make install && sudo ./install.sh
```

> **Примечание** на Arch/EndeavourOS: `install.sh` автоматически найдёт
> каталог `/usr/lib/security/` (Arch) или `/usr/lib/x86_64-linux-gnu/security/`
> (Debian) — сильно вручную выбирать не нужно.

> **Автоматизация install.sh**: сам скрипт ищет и устанавливает модуль,
> бэкапит `/etc/pam.d/lightdm` в `lightdm.pammath.bak`, добавляет строку
> `auth optional pam_math_challenge.so` (idempotent — повторный запуск
> ничего не сломает), и чистит после себя при `--uninstall`.

### GUI-установка

`install.sh` умеет корректно брать root-через-GUI: если вы запускаете
его из терминала **внутри графической сессии** и `sudo` недоступен
(например, чистая Arch + `polkit`), он вызывает `pkexec` с графическим
диалогом запроса пароля — тогда «настройка через GUI» тоже работает.

## Конфигурация

`/etc/security/math_challenge.conf`:

```ini
difficulty = easy|medium|hard
attempts = 3                # попыток в одной сессии входа
cooldown_seconds = 300      # время блокировки после провала
exempt_users = user1,user2  # кого не спрашивать
state_dir = /run/math_challenge
ignore_errors = false       # PAM_IGNORE вместо PAM_SUCCESS после верного ответа
```

## Удаление

```bash
sudo ./install.sh --uninstall
```

Деактивирует модуль, восстанавливает `/etc/pam.d/lightdm` из бэкапа, удаляет
`.so` и конфиг. Вы по любому можете просто вручную убрать строку из
`/etc/pam.d/lightdm`, чтобы временно отключить.

## Тестирование (не заблокировать себя)

1. Откройте ssh-сессию или root-tty — «спасательный» вход, не зависящий от lightdm.
2. НЕ трогайте `/etc/pam.d/common-auth`, чтобы `sudo` не пострадал.
3. Сначала: `sudo ./install.sh`, затем обычный логин: логин + пароль → пример.
4. Тест блокировки: 3× неверный ответ → «Учётная запись заблокирована».
   Повторная попытка входа → сразу отказ, без примера.
5. Тест exempt: добавьте пользователя в `exempt_users=` — челленджа не будет.
6. Тест fail-open: `sudo mv /etc/security/math_challenge.conf{,.bak}` — модуль
   уходит в fail-open, syslog пишет `conf missing/invalid, fail-open`, вход работает.
7. Сброс state: `sudo rm -f /run/math_challenge/<user>` (или ребут).
8. Журнал: `journalctl -u lightdm -f` или `/var/log/lightdm/lightdm.log`.

## Восстановление, если заперлись

- GRUB → `e` → допишите к строке `linux ...` → `init=/bin/bash` → Ctrl+X
- `mount -o remount,rw /`
- правьте `/etc/pam.d/lightdm`, уберите `pam_math_challenge.so` строку (или `rm -f /run/math_challenge/<user>`)
- `sync; reboot`

## Что в репозитории

```
pam_math_challenge.c    — исходник модуля
Makefile                — сборка с relro/stack-protector; авто-поиск security/
math_challenge.conf     — пример /etc/security/math_challenge.conf
install.sh              — автосборка, автоподключение PAM-стека, backup/uninstall
README.md               — этот файл
```

## Безопасность

Сборка с `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, relro/now.
Ответ примера живёт только на стеке внутри итерации цикла; буфер ответа
обнуляется `secure_zero`-ом сразу после проверки.
