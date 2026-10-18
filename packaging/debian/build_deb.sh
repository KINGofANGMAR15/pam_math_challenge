#!/bin/bash
# build_deb.sh — сборка .deb пакета без debhelper (только dpkg-deb).
#
# Usage:
#   ./packaging/debian/build_deb.sh [--skip-build]
#
# Результат: pam-math-challenge_<pkgver>_amd64.deb рядом со скриптом.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PKGDIR="$ROOT/packaging/debian"
STAGE="$PKGDIR/staging"
NAME="pam-math-challenge"
VERSION="1.2.0-1"
ARCH="amd64"
OUT="$ROOT/${NAME}_${VERSION}_${ARCH}.deb"

info()  { printf '\033[1;32m[+]\033[0m %s\n' "$*"; }
die()   { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

command -v dpkg-deb >/dev/null 2>&1 || die "dpkg-deb не найден (нужен Debian/Ubuntu/Mint)"
command -v gcc >/dev/null 2>&1 || die "gcc не найден"

# multiarch-каталог — Debian-специфика (on Mint: /usr/lib/x86_64-linux-gnu/security)
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo x86_64-linux-gnu)"
info "multiarch: $MULTIARCH"

# ---------- сборка ----------
if [ "${1:-}" != "--skip-build" ]; then
    info "Сборка модуля..."
    (cd "$ROOT" && make clean && make)
fi
[ -f "$ROOT/pam_math_challenge.so" ] || die "не найден pam_math_challenge.so — сначала make"

# ---------- стейджинг ----------
rm -rf "$STAGE"
mkdir -p \
    "$STAGE/DEBIAN" \
    "$STAGE/usr/lib/$MULTIARCH/security" \
    "$STAGE/usr/sbin" \
    "$STAGE/etc/security" \
    "$STAGE/usr/share/doc/$NAME"

info "Копирование файлов..."
install -m 0755 "$ROOT/pam_math_challenge.so"   "$STAGE/usr/lib/$MULTIARCH/security/"
install -m 0755 "$ROOT/pam-math-challenge"      "$STAGE/usr/sbin/pam-math-challenge"
install -m 0644 "$ROOT/math_challenge.conf"     "$STAGE/etc/security/math_challenge.conf"
install -m 0644 "$ROOT/README.md" "$ROOT/Makefile" "$STAGE/usr/share/doc/$NAME/"
gzip -n -9 "$STAGE/usr/share/doc/$NAME/README.md" || true
# копируем readme с оригинальным именем, не gzip'ом (читабельность)
install -m 0644 "$ROOT/README.md" "$STAGE/usr/share/doc/$NAME/README"

# tmpfiles для /run/math_challenge
install -Dm0644 "$ROOT/tmpfiles.conf" \
    "$STAGE/usr/lib/tmpfiles.d/pam-math-challenge.conf"

# control-файлы
install -m 0644 "$PKGDIR/control"    "$STAGE/DEBIAN/control"
install -m 0644 "$PKGDIR/conffiles"  "$STAGE/DEBIAN/conffiles"

for hook in postinst prerm postrm; do
    install -m 0755 "$PKGDIR/$hook" "$STAGE/DEBIAN/$hook"
done

# ---------- сборка .deb ----------
info "Сборка $OUT ..."
dpkg-deb --build --root-owner-group "$STAGE" "$OUT"

info "OK: $OUT"
info "Установка: sudo dpkg -i $OUT"
info "Проверка:  dpkg -L $NAME; sudo pam-math-challenge status"
