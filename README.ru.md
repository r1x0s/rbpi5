# rbpi5 — rekordbox (XDJ-RX3) на Raspberry Pi 5

[English](README.md) · **Русский**

Штатный плеер rekordbox из **Pioneer DJ XDJ-RX3** (`rbp`, прошивка v1.20) на
**Raspberry Pi 5** с 7" сенсорным экраном и контроллером **DDJ-FLX4** или
**DDJ-GRV6**. Всё настоящее: интерфейс, браузер, эффекты и USB-библиотека
плеера. Управление — с пульта, звук — через аудиокарту пульта.

Это форк [salesforcedoc/rbpi4b](https://github.com/salesforcedoc/rbpi4b)
(Raspberry Pi 4), отсюда и название: **rb** — так плеер называется внутри
прошивки, **pi5** — плата. В него влит порт на Pi 5 из
[stableneo/rbpi5-grv6](https://github.com/stableneo/rbpi5-grv6), добавлены
карта DDJ-GRV6 и комплект «сборка и установка одной командой». Благодарности —
в [SOURCES.ru.md](SOURCES.ru.md).

> **Сначала прочитайте [NOTICE.ru.md](NOTICE.ru.md).** В репозитории **нет**
> прошивки, ключей, бинарника `rbp` и других файлов Pioneer/AlphaTheta. Он
> работает только с файлами, которые **вы** предоставляете сами. Как их получить
> и можно ли это делать там, где вы живёте, решаете вы и отвечаете за это вы.
> Всё, что вы делаете с проектом, вы делаете на свой риск.

```
ваше дерево XDJ-RX3 v1.20 ─► make assets ─► make build ─► make pi-setup ─► make deploy
   (предоставляете вы)       extracted/      work/*.tgz     Pi 5, один раз   /opt/rblive4
```

## Что нужно

| | |
|---|---|
| **Raspberry Pi 5** | 4 ГБ хватает. Нужен официальный БП 27 Вт: DDJ-FLX4 питается от USB Pi. |
| **ОС на Pi** | Raspberry Pi OS **Lite 64-bit** (trixie), SSH включён, пользователь с sudo |
| **Экран** | HDMI, лучше 1280×800 (другое разрешение — с масштабом), USB-тач |
| **Контроллер** | DDJ-FLX4 и/или DDJ-GRV6 (GRV6 — только от своего адаптера питания) |
| **Рабочая машина** | Linux (x86_64 или arm64) с **Docker** и **git**, ~5 ГБ места |
| **Ассеты** | ваше **уже извлечённое** дерево XDJ-RX3 **v1.20**: каталог, где лежат `gui/` и `images/` (`rootfs.cramfs`, `gui.tar.gz`, `pdj.tar.gz`). См. [docs/pi5/assets.ru.md](docs/pi5/assets.ru.md) |

## Быстрый старт

```sh
git clone https://github.com/r1x0s/rbpi5
cd rbpi5
cp pi.env.example pi.env           # адрес Pi, разъём HDMI, поворот
make all SRC=/path/to/XDJRX3       # ассеты → сборка → настройка Pi → установка
```

`make all` один раз спросит, перезагрузить ли Pi: это нужно после смены ядра.
После установки Pi загружается сразу в rekordbox.

## По шагам

| Шаг | Команда | Что делает | Где |
|---|---|---|---|
| 1 | `make assets SRC=…` | из вашего дерева XDJ-RX3: распаковывает rootfs и GUI, проверяет md5 штатного `rbp`, накладывает патчи совместимости → `extracted/` | ПК, Docker |
| 2 | `make build` | DirectFB 1.4.16, шимы и их тесты, chroot → `work/rbpi4b-pi4.tgz` | ПК, Docker |
| 3 | `make pi-setup` | `kernel8.img` (страницы 4 КБ), режим и поворот экрана, пакеты, `rb.local.conf`, обход USB для FLX4; предлагает перезагрузку | Pi по SSH |
| 4 | `make deploy` | ставит плеер в `/opt/rblive4` и запускает его | Pi по SSH |
| — | `make doctor` | проверка состояния на Pi (только чтение) | Pi |

Любой шаг можно запустить повторно: повторный запуск обновляет то, что уже стоит.

## Настройки (`pi.env`)

| Ключ | Значения | |
|---|---|---|
| `PI_HOST` | `pi@192.168.x.y` | куда ставить |
| `PANEL_CONNECTOR` | `HDMI-A-1` / `HDMI-A-2` | HDMI-A-1 — разъём рядом с USB-C питания |
| `PANEL_ROTATE` | `0` / `180` | по умолчанию `0`; `180` — только для перевёрнутой панели, см. ниже |
| `MIDI_MAP` | `auto` / `flx4` / `grv6` | `auto` — карта того пульта, который подключён при запуске |
| `USB_POWERCYCLE` | `1` / `0` | передёрнуть питание USB при загрузке, если пульт не определился (нужно FLX4 с питанием от USB) |

### Экран вверх ногами (необязательно)

Нужно, только если панель стоит перевёрнутой: многим 7" HDMI-панелям так удобнее
вывести кабели. При обычной установке ничего делать не надо, по умолчанию
`PANEL_ROTATE=0`.

Если картинка и тач перевёрнуты:

1. В `pi.env`: `PANEL_ROTATE=180`.
2. `make pi-setup`. Он запишет в `cmdline.txt`
   `video=<разъём>:1280x800@60,rotate=180` (картинку поворачивает ядро), а в
   `/opt/rblive4/rb.local.conf` — `RB_POINT_INVERT_X=1` и `RB_POINT_INVERT_Y=1`
   (тач), потом предложит перезагрузку.

Вернуть обратно: `PANEL_ROTATE=0` и снова `make pi-setup`. Если тач промахивается
только по одной оси, переверните её вручную в `rb.local.conf` и выполните
`sudo systemctl restart rblive4`.

### Тонкие настройки

Настройки плеера (звук, джог, LED и т. д.) лежат на Pi в
`/opt/rblive4/rb.local.conf`. Описание каждого ключа — в
[scripts/device/rb.conf](scripts/device/rb.conf). Установка этот файл не
перезаписывает.

## Работа

- **Сменить пульт:** при `MIDI_MAP=auto` подключите другой и перезапустите плеер
  (`sudo systemctl restart rblive4`).
- **Поменять маппинг:** маппинги — это код на C, он компилируется в шим. Это не
  отдельные файлы рядом с плеером. Правите `scripts/shims/map_flx4.c` или
  `map_grv6.c` и выполняете `make shims`: он соберёт шимы, прогонит тесты, положит
  их на Pi и перезапустит плеер за секунды. Подробнее —
  [docs/pi5/mapping.ru.md](docs/pi5/mapping.ru.md).

## Состояние

Работает на Pi 5 автора с 2026-10-23: Pi OS trixie 64-bit, HDMI-панель 1280×800 с
поворотом 180°, DDJ-FLX4 и DDJ-GRV6. Длительные сеты (≥ 1 ч без перезапуска)
**ещё не подтверждены**. Что измерено, что нет и известные ограничения —
[docs/pi5/hardware.ru.md](docs/pi5/hardware.ru.md).

## Что где лежит

| Путь | |
|---|---|
| `Makefile`, `pi.env.example`, `pi5/` | комплект этого форка: Docker-образ сборки, скрипты сборки, настройки Pi и установки |
| `scripts/shims/` | LD_PRELOAD-шимы: экран, звук, маппинги пультов, LED (C, с тестами) |
| `scripts/device/` | то, что работает на Pi: `install.sh`, `start-rb.sh`, `doctor.sh`, `rb.conf` |
| `scripts/build-chroot.sh`, `tools/patch-rbp/`, `tools/build-directfb/` | сборка chroot, патчи совместимости `rbp`, патч DirectFB |
| `docs/` | документация upstream (Pi 4, протоколы, bring-up, на английском); `docs/pi5/` — этого форка |
| [README-rbpi4b.md](README-rbpi4b.md), [TUTORIAL.md](TUTORIAL.md) | README и ручная инструкция upstream (Pi 4) |

## Перед `git push`

```sh
make check      # в git нет того, что должно остаться у вас (прошивка, ключи, бинарники)
```

Как опубликовать свою копию — [docs/pi5/publish.ru.md](docs/pi5/publish.ru.md).

## Лицензия

MIT ([LICENSE](LICENSE)) — для кода этого репозитория. Сторонние компоненты и их
лицензии — в [NOTICE.md](NOTICE.md).
