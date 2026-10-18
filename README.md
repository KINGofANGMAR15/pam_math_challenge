# pam_math_challenge

PAM-модуль для Linux, который добавляет **математический челлендж как второй
фактор входа** и умеет **временно блокировать учётную запись** при исчерпании
попыток.

Поддерживаемые окружения:

| Дистрибутив | Display manager / экран блокировки | Статус |
|---|---|---|
| Linux Mint, Ubuntu, Debian | LightDM + Slick Greeter | ✅ основная цель |
| Arch/EndeavourOS + LightDM | LightDM / Slick Greeter | ✅ поддерживается |
| Arch/EndeavourOS + KDE Plasma | **plasmalogin** (`/usr/lib/pam.d/plasmalogin`) + лок-скрин **kscreenlocker** (сервис `kde`) | ✅ поддерживается (v1.1.0+, проверено на EndeavourOS) |
| XFCE / GNOME / MATE / Cinnamon | `xfce4-screensaver` / `gnome-screensaver` (в т.ч. `light-locker`) / `mate-screensaver` / `cinnamon-screensaver` | ✅ поддерживается (v1.1.0+) |
| любая TTY-сессия | `login` / ssh | ⚠️ работает (текстовый диалог) |
| GDM | GDM | ⚠️ работает, не протестировано |
| SDDM, LXDM, Entrance | эти DM | ❌ не поддерживается (нет многошаговых PAM-диалогов) |

**Как helper находит ваши стеки.** Хелпер проверяет оба каталога:
`/etc/pam.d/` (файлы администратора и базовый стек pambase) и `/usr/lib/pam.d/`
(vendor-файлы, поставляемые пакетами; схема PAM 1.5.3+). Сами vendor-файлы
не изменяются — их правки потерялись бы при `pacman -Syu`. Вместо этого
создаётся override `/etc/pam.d/<сервис>`, который имеет приоритет:
*«All files in /etc/pam.d/ override files with the same name in other
directories»* (`man 8 pam`, `man 5 pam.d`). После обновления пакета
(например, Plasma) override регенерируется командой `refresh`.

«Графичность» обеспечивается самим greeter/лок-скрином: модуль не рисует
собственный GUI, а передаёт вопрос через `pam_conv`. Greeter/Slick Greeter рисует
его как поле ввода на экране входа, tty — как обычное read-line сообщение.

## Как работает

1. Пользователь вводит пароль (проверяет `pam_unix` / greeter-стек);
2. `pam_math_challenge` вызывается **после** пароля из PAM-стека сервиса входа
   (`lightdm`, `plasmalogin`, …);
3. greeter показывает пример (Easy / Medium / Hard);
4. верный ответ → `PAM_SUCCESS`/`PAM_IGNORE`, неверный ответ → часть attempts;
   исчерпал попытки → блокировка (`lock_until=now+cooldown`) и отказ входа;
5. при попытке входа в течение `cooldown_seconds` → сразу отказ, без вопроса.

Экран блокировки (lock screen) проходит через **другой** PAM-сервис
(например, `kde` для KDE Plasma / kscreenlocker, `xfce4-screensaver` для XFCE):
если его не патчить — разблокировка пускает без челленджа. Хелпер v1.1.0+
автоматически патчит и вход, и все обнаруженные экраны блокировки.

Состояние хранится в `/run/math_challenge/<user>` (root-only, очищается при
ребуте). Блокировку можно сбросить удалением этого файла.

## Установка

### Через пакет (рекомендуется)

> Версии вида `1.1.0-1` в командах ниже — «версия pkgver-pkgrel»; на выходе
> сборки имя файла указывается в итоговом выводе, используйте его настоящим.

**Mint / Ubuntu / Debian** — сборка и установка .deb (на самой системе с dpkg):

```bash
./packaging/debian/build_deb.sh
sudo dpkg -i pam-math-challenge_1.1.0-1_amd64.deb
# при неудовлетворённых зависимостях: sudo apt -f install
```

`postinst` пакета сам подключит модуль ко **всем найденным** стекам
(вход + экран блокировки, с бэкапом/override'ами). При `dpkg -r`/`purge`
скрипт `prerm`/`postrm` сначала уберут строки и override'ы и только затем
удалят `.so` — стек никогда не остаётся битым.

**Arch / EndeavourOS** — сборка и установка `pkg.tar.zst`:

```bash
makepkg -f
sudo pacman -U pam-math-challenge-1.1.0-1-x86_64.pkg.tar.zst
```

pacman scriptlet (`pam-math-challenge.install`) делает то же самое через
`post_install`/`pre_remove`. Конфиг объявлен в `backup=()`, поэтому pacman
не затрёт ваши правки в `/etc/security/math_challenge.conf`.После установки любым способом управлять можно хелпером:

```bash
sudo pam-math-challenge enable     # подключить ВООБЩЕ ВСЕ найденные стеки
                                   # (логин + экран блокировки)
sudo pam-math-challenge disable   # отключить отовсюду
pam-math-challenge status         # какой стек найден, где активен
sudo pam-math-challenge refresh   # регенерировать override'ы /etc/pam.d
                                  # после обновления Plasma/KDE и др.
sudo pam-math-challenge restart   # перезапуск DM (СБРОСИТ СЕССИЮ!)
```

Если ваш лок-скрин не входит в стандартный список, можно определить нужный
PAM-сервис самостоятельно:

```bash
# список всех сервисов:
ls /etc/pam.d/ /usr/lib/pam.d/
# svcname обычно совпадает с именем программы экрана блокировки
```

Кандидатов (`kde`, `xfce4-screensaver`, `xscreensaver`, …) нигде дописывать
не нужно: `sudo pam-math-challenge refresh` подхватит появившийся
`/usr/lib/pam.d/<имя>` при следующем обновлении списка хелпера.

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

Через пакетный менеджер (если ставили пакетом — рекомендуемый путь):

```bash
# Mint / Ubuntu / Debian
sudo dpkg -r pam-math-challenge        # или --purge, чтобы убрать и бэкап
# Arch / EndeavourOS
sudo pacman -Rns pam-math-challenge
```

Порядок безопасный: скрипты пакета (`prerm`/`pre_remove`) **сначала** убирают
строку из `/etc/pam.d/lightdm`, и только после этого удаляется `.so` — стек
никогда не остаётся битым (проверено в Docker-контейнере Debian 12).

Ручной вариант (для установки из исходников через `install.sh`):

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

Если модуль ставился пакетом — можно загрузиться в tty и сделать
`sudo dpkg -r pam-math-challenge` / `sudo pacman -Rns pam-math-challenge`.

## Что в репозитории

```
pam_math_challenge.c        — исходник модуля
Makefile                    — сборка с relro/stack-protector; авто-поиск security/
math_challenge.conf         — пример /etc/security/math_challenge.conf
pam-math-challenge          — CLI-хелпер: enable/disable/status/restart
install.sh                  — установка из исходников (без пакетного менеджера)
PKGBUILD                    — сборка pkg.tar.zst для Arch / EndeavourOS
pam-math-challenge.install  — pacman-скриптлет (post_install/pre_remove)
packaging/debian/           — сборка .deb: control, postinst, prerm, postrm,
                              conffiles, build_deb.sh
README.md                   — этот файл
```

## Сборка и тестирование пакета

Протестировано:
- `pkg.tar.zst` — собран `makepkg` на EndeavourOS (layout `/usr/lib/security/` — OK).
- `.deb` — собран и прогнан в Docker-контейнере Debian 12: `postinst` сам
  правит `/etc/pam.d/lightdm` (обратимо, с бэкапом), удаление пакета
  оставляет стек нетронутым.

Можно повторить контейнерный тест самостоятельно:

```bash
cd pam_math_challenge
sudo docker run --rm -v "$PWD:/src" -w /src debian:12 bash -c "
    apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    build-essential libpam0g-dev dpkg-dev && ./packaging/debian/build_deb.sh"
```

## Безопасность

Сборка с `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, relro/now.
Ответ примера живёт только на стеке внутри итерации цикла; буфер ответа
обнуляется `secure_zero`-ом сразу после проверки.
