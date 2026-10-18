# pam_math_challenge — второй фактор входа (математический челлендж)

Модуль Linux-PAM для Linux Mint (LightDM / Slick Greeter). После успешной
проверки пароля greeter показывает математический пример; при исчерпании
попыток учётная запись временно блокируется.

## 1. Сборка

```bash
sudo apt install build-essential libpam0g-dev
cd pam_math_challenge
make
make test-conf     # убедиться, что .so линкуется без undefined symbols
```

## 2. Установка

```bash
sudo make install          # копирует .so и (если нет) конфиг
```

`.so` окажется в `/usr/lib/x86_64-linux-gnu/security/pam_math_challenge.so`.

Проверьте реальный `security/` каталог на вашей системе:

```bash
dpkg -L libpam-modules:amd64 | grep -m1 '/security/'
```

## 3. Конфигурация

`/etc/security/math_challenge.conf` (см. `math_challenge.conf` в репо):

- `difficulty` = easy | medium | hard
- `attempts` = 3 (число попыток в одной сессии входа)
- `cooldown_seconds` = 300 (сколько секунд аккаунт заблокирован)
- `exempt_users` = user1,user2 (кто не проходит челлендж)
- `state_dir` = /run/math_challenge
- `ignore_errors` = false (указать true => PAM_IGNORE вместо PAM_SUCCESS)

## 4. Подключение в PAM-стек

Важно: модуль добавляется ПОСЛЕ проверки пароля (как второй фактор),
а не вместо неё.

**Вариант A (рекомендуемый для начала): optional + ignore_errors=true**
```pam
auth  required            pam_permit.so   # НЕ редактируйте эту строку, пример-иллюстрация
# пароль уже проверили pam_unix / lightdm-auth ранее в стеке
# добавьте В САМОМ КОНЦЕ auth-секции файла /etc/pam.d/lightdm:
auth  optional            pam_math_challenge.so
```

**Вариант B: required (жёсткий второй фактор)** — то же самое, но
`required`. Тогда без корректного ответа вход невозможен.

Не вставляйте модуль **до** `pam_unix` — тогда челлендж появится до
пароля, и смысла во втором факторе нет.

## 5. Перезапуск LightDM

`lightdm` читает PAM-стек на каждой новой попытке логина, поэтому пере-
запуск не обязателен, но после установки lib/so безопаснее сделать:

```bash
sudo systemctl restart lightdm
```

⚠️ Это выкинет текущую графическую сессию. Сделайте это из tty (Ctrl+Alt+F1).

## 6. Диагностика

```bash
journalctl -u lightdm -f     # или: cat /var/log/lightdm/lightdm.log
syslog | grep math_challenge
```

## 7. Тестирование (не заблокировать себя)

1. **Тест через tty + ssh-сессию.** Добавьте модуль только в
   `/etc/pam.d/lightdm`. Откройте второй терминал по ssh (или tty), это
   ваш «спасательный» вход, не зависящий от lightdm.
2. **Сухой прогон через sudo su.** Так как модуль по умолчанию НЕ в
   `/etc/pam.d/common-auth`, он никак не заденет sudo.
3. **Проверка в lightdm.** Выйдите из X-сессии (не переzaпуская lightdm),
   введите логин/пароль. Greeter должен показать пример.
4. **Тест блокировки.** 3 раза введите неверный ответ → появится
   «Учётная запись заблокирована». Сразу же попробуйте войти ещё раз —
   диалог примера НЕ появится, сразу отказ.
5. **Тест exempt.** Добавьте своего пользователя в `exempt_users=` и
   войдите — челленджа быть не должно.
6. **Очистка состояния.** `sudo rm -f /run/math_challenge/<user>` — state
   файл обнулит streak+блокировку немедленно (или просто ребут: /run).
7. **Fail-open тест.** `sudo mv /etc/security/math_challenge.conf{,.bak}`
   → вход должен работать как раньше, а в syslog придёт
   `conf missing/invalid, fail-open`.

## 8. Восстановление доступа при «заперся»

- Загрузитесь с `init=/bin/bash`
  (GRUB: `e`, к строке `linux` допишите, Ctrl+X)
- `mount -o remount,rw /`
- `rm -f /run/math_challenge/<user>` или правьте `/etc/pam.d/lightdm`,
  уберите строку `pam_math_challenge.so`, `sync`, `reboot` — либо просто
   уберите строку из PAM-стека.

## 9. Резервный вход, если что-то пошло не так

Всегда держите открытым ssh-сессию или root-tty, пока тестируете. Убедитесь,
что `/etc/pam.d/common-auth` остался нетронутым — он же влияет на `sudo`
(иначе есть шанс потерять root-доступ тоже).
