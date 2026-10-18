# Makefile для pam_math_challenge
# Поддерживаемые дистрибутивы:
#   Debian / Ubuntu / Linux Mint  (libpam0g-dev, security/ в multiarch-каталоге)
#   Arch Linux / EndeavourOS      (pam header'ы, security/ в /usr/lib/security)
#
# Путь к каталогу PAM-модулей определяется автоматически на этапе install
# (см. переменную SECURITY_DIR, которая резолвится внутри правила).

CC         := gcc
CFLAGS     := -Wall -Wextra -O2 -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
              -Wformat -Wformat-security -fPIE
LDFLAGS    := -shared -Wl,-z,relro,-z,now
LDLIBS     := -lpam

TARGET     := pam_math_challenge.so
CONF       := /etc/security/math_challenge.conf
CONF_LOCAL := math_challenge.conf

# Каталоги, где может лежать PAM .so, в порядке приоритета.
# Arch: /usr/lib/security; Debian/Ubuntu/Mint: multiarch-каталог.
PAM_LIB_DIRS := /usr/lib/x86_64-linux-gnu/security \
                /usr/lib/security \
                /usr/lib64/security \
                /lib/security

.PHONY: all clean install install-conf uninstall test-conf

all: $(TARGET)

$(TARGET): pam_math_challenge.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Резолвим путь на этапе выполнения (не хардкодим за дистрибутив).
resolve-security-dir:
	@for d in $(PAM_LIB_DIRS); do \
	    if [ -d "$$d" ]; then echo "$$d"; exit 0; fi; \
	done; \
	echo "ERROR: not found pam security dir" >&2; exit 1

install: $(TARGET) install-conf
	@SEC_DIR=$$( $(MAKE) --no-print-directory resolve-security-dir ); \
	echo "Installing $(TARGET) -> $$SEC_DIR"; \
	install -d "$$SEC_DIR"; \
	install -m 0755 $(TARGET) "$$SEC_DIR/$(TARGET)"; \
	echo "OK. Теперь запустите ./install.sh для подключения в /etc/pam.d/lightdm."

install-conf:
	if [ ! -f $(CONF) ]; then \
	    install -d /etc/security; \
	    install -m 0644 $(CONF_LOCAL) $(CONF); \
	    echo "Конфиг установлен: $(CONF)"; \
	else \
	    echo "Конфиг уже существует, не трогаем: $(CONF)"; \
	fi

uninstall:
	@for d in $(PAM_LIB_DIRS); do \
	    if [ -f "$$d/$(TARGET)" ]; then rm -v "$$d/$(TARGET)"; fi; \
	done
	if [ -f $(CONF) ]; then rm -v $(CONF); fi
	@echo "Не забудьте удалить строку из /etc/pam.d/lightdm (см. ./install.sh --uninstall)."

clean:
	rm -f $(TARGET)

# Проверка, что .so линкуется без неразрешённых символов
test-conf: $(TARGET)
	ldd -r $(TARGET) 2>&1 | grep -i undef || echo "OK: нет неразрешённых символов"
