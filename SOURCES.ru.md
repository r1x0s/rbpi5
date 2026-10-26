# Источники и благодарности

[English](SOURCES.md) · **Русский**

Проекты и материалы, на которых построен этот форк. Спасибо их авторам.

## Основа

| Проект | Что взято |
|---|---|
| [@i.erhan.es](https://instagram.com/i.erhan.es) | исходная основа кода rbpi4b (так указано в upstream) |
| [salesforcedoc/rbpi4b](https://github.com/salesforcedoc/rbpi4b) (MIT) | весь подход: soft-float chroot + LD_PRELOAD-шимы, патчи совместимости `rbp`, патч DirectFB, `install.sh` / `doctor.sh`, таблица контроллеров, карта FLX4. Этот репозиторий — его форк |
| [stableneo/rbpi5-grv6](https://github.com/stableneo/rbpi5-grv6) | порт на Pi 5: `kernel8.img` (страницы 4 КБ), 16 bpp вместо 32 (oops в `iommu_dma_mmap`), `docs/port/pi5-bringup.md`; его ветка `port/grv6` — база `pi5-flx4` |
| [mutlisensor/Rx3-flx4](https://github.com/mutlisensor/Rx3-flx4) | соседний порт XDJ-RX3 + FLX4 на Pi 5 (мост на Python): идеи, keepalive FLX4 |

Проекты, которые upstream называет предшественниками (см. [NOTICE.md](NOTICE.md)):
порт XDJ-RX3 → Denon Prime GO *PrimeBox* (патчер `rbp`, патч DirectFB fbdev, общие
шимы), *rb2go* (телефон на postmarketOS), *chromebit* (ASUS Chromebit, RK3288),
*freelive4* (SC Live 4).

## Сборка

| | |
|---|---|
| [deniskropp/DirectFB](https://github.com/deniskropp/DirectFB) | DirectFB 1.4.16, коммит `3637392`, LGPL-2.1 |
| [deniskropp/flux](https://github.com/deniskropp/flux) | компилятор IDL `fluxcomp`, нужен для сборки DirectFB, коммит `10ad2eb` |
| Debian bookworm `gcc-arm-linux-gnueabi`, `qemu-user` | soft-float тулчейн и запуск ARM-тестов на ПК |

## Контроллеры (MIDI, LED)

| | |
|---|---|
| [Mixxx: Pioneer-DDJ-FLX4.midi.xml](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Pioneer-DDJ-FLX4.midi.xml), [script](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Pioneer-DDJ-FLX4-script.js) | справочник MIDI FLX4, сверен с замерами |
| [joergbeyer/dj-controller](https://github.com/joergbeyer/dj-controller) | маппинг DDJ-GRV6 для Mixxx: справочник нот и CC GRV6 |
| [Форум Mixxx: Working on a Pioneer DDJ-GRV6 mapping](https://mixxx.discourse.group/t/working-on-a-pioneer-ddj-grv6-mapping/33114) | обсуждение MIDI GRV6 |
| [GitHubuser5o5/mixxx-grv6-build](https://github.com/GitHubuser5o5/mixxx-grv6-build) | ещё одна сборка маппинга GRV6 |
| [DDJ-GRV6 Operating Instructions (PDF)](https://downloads.support.alphatheta.com/manuals/dj-controllers/DDJ-GRV6/DDJ-GRV6_DRI1927A_manual.pdf), [страница продукта](https://alphatheta.com/en/product/dj-controller/ddj-grv6/black/) | названия и функции органов управления GRV6 |

Всё, что в картах помечено *measured*, снято на настоящих пультах (`aseqdump`,
`tools/grv6-capture.sh`, `tools/grv6-ledprobe.sh`). Таблицы и фикстуры лежат в
`docs/15-flx4-midi.md`, `docs/port/grv6-midi.md` и `scripts/shims/tests/`.

## Raspberry Pi

| | |
|---|---|
| [config.txt и параметры загрузки](https://www.raspberrypi.com/documentation/computers/configuration.html) | `kernel=`, `video=` в `cmdline.txt` |
| [Raspberry Pi OS](https://www.raspberrypi.com/software/operating-systems/) | ОС на Pi (Lite 64-bit, trixie) |
| [mvp/uhubctl](https://github.com/mvp/uhubctl) | выключение и включение питания USB при загрузке: без этого FLX4 на Pi 5 не определяется |

## Прошивка XDJ-RX3

Файл обновления прошивки производитель публикует на страницах поддержки
XDJ-RX3 ([pioneerdj.com](https://www.pioneerdj.com/) / [alphatheta.com](https://alphatheta.com/)),
например [новость о прошивке 2024 г.](https://www.pioneerdj.com/ja/news/2024/xdj-xz-xdj-rx3-xdj-rr-and-xdj-rx2-firmware/).
Проект ожидает **уже извлечённое** дерево v1.20. Как его получить и расшифровать,
и любые ключи — вне проекта и здесь не описаны. Это ответственность пользователя
([NOTICE.ru.md](NOTICE.ru.md)).
