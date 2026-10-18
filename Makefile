# Makefile для pam_math_challenge
# Debian/Ubuntu/Mint: libpam0g-dev кладёт pam_modules.h в /usr/include/security/

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
           -Wformat -Wformat-security -fPIE
LDFLAGS := -shared -Wl,-z,relro,-z,now
LDLIBS  := -lpam

TARGET  := pam_math_challenge.so
DESTLIB := /usr/lib/x86_64-linux-gnu/security/
CONF    := /etc/security/math_challenge.conf

.PHONY: all clean install test-conf

all: $(TARGET)

$(TARGET): pam_math_challenge.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

install: $(TARGET)
	install -d $(DESTLIB)
	install -m 0755 $(TARGET) $(DESTLIB)$(TARGET)
	if [ ! -f $(CONF) ]; then install -m 0644 math_challenge.conf $(CONF); fi
	@echo "Не забудьте добавить строку в /etc/pam.d/lightdm и перезапустить LightDM."

clean:
	rm -f $(TARGET)

# Быстрая проверка, что .so подхватывается линковщиком и PAM-симболы на месте
test-conf: $(TARGET)
	ldd -r $(TARGET) 2>&1 | grep -i undef || echo "OK: нет неразрешённых символов"
