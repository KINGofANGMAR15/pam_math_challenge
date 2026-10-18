# Maintainer: pam-math-challenge packagers
# Arch / EndeavourOS package: makepkg -> pkg.tar.zst
#
# Сборка (внутри каталога проекта pam_math_challenge/):
#   makepkg -f
#   sudo pacman -U pam-math-challenge-1.0.0-1-x86_64.pkg.tar.zst
#
# Если проект клонирован из git, можно также сделать makepkg с git-source —
# см. комментарий ниже.

pkgname=pam-math-challenge
pkgver=1.0.0
pkgrel=1
pkgdesc="Математический чекап как второй фактор входа через PAM (LightDM)"
arch=('x86_64')
url="https://github.com/your-name/pam_math_challenge"
license=('MIT')  # замените при необходимости на фактическую лицензию проекта
depends=('glibc' 'pam')
optdepends=('lightdm: менеджер входа с поддержкой многошаговых PAM-диалогов'
            'lightdm-slick-greeter: рекомендуемый greeter')
backup=('etc/security/math_challenge.conf')
install=pam-math-challenge.install

# Локальные файлы проекта. makepkg копирует их в $srcdir.
source=(
    "pam_math_challenge.c"
    "Makefile"
    "math_challenge.conf"
    "pam-math-challenge"
    "README.md"
)
# Для сборки прямиком из git замените блок source на:
#   source=("$pkgname::git+https://github.com/your-name/pam_math_challenge.git")
# и в build()/package() используйте "$srcdir/$pkgname".
sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP')

build() {
    make clean >/dev/null
    make
}

# Arch: PAM .so живёт в /usr/lib/security/ (без multiarch, в отличие от Debian).
package() {
    local sec_dir="/usr/lib/security"

    install -Dm0755 "pam_math_challenge.so" \
        "$pkgdir/$sec_dir/pam_math_challenge.so"
    install -Dm0755 "pam-math-challenge" \
        "$pkgdir/usr/bin/pam-math-challenge"
    install -Dm0644 "math_challenge.conf" \
        "$pkgdir/etc/security/math_challenge.conf"
    install -Dm0644 "README.md" \
        "$pkgdir/usr/share/doc/$pkgname/README.md"
}
