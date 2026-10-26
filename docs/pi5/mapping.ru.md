# Маппинги контроллеров

[English](mapping.md) · **Русский**

## Как это устроено

Маппинг — это **код на C**, а не файл, который кладут рядом с плеером. Он
собирается в шим `knobshim.so`. Шим через LD_PRELOAD сидит внутри `rbp`, читает
MIDI пульта из ALSA sequencer и превращает его в нажатия и значения, которые
понимает `rbp`. LED идут в обратную сторону.

```
DDJ-FLX4 / DDJ-GRV6 ──MIDI──► knobshim.so (map_flx4.c | map_grv6.c) ──► rbp
                    ◄──LED───  rbp_led.c, led_table.c               ◄──
```

| Файл (`scripts/shims/`) | Что в нём |
|---|---|
| `controllers.c` | одна строка на пульт: имя карты, подстрока MIDI-порта, ALSA card id, USB id, keepalive. По этой таблице `RB_MIDI_MAP=auto` узнаёт подключённый пульт |
| `map_flx4.c`, `map_grv6.c` | ноты и CC пульта → действия `rbp` (что делает каждая кнопка) |
| `map_ux.c` | общее поведение обеих карт: открыть и закрыть браузер, кнопка CFX, SHIFT + джог = поиск, удержание «назад в меню» |
| `rbp_led.c`, `led_table.c`, `ctrl_map.h` (`struct led_notes`) | какой LED на какой ноте; `-1`/`0` — такого LED нет |
| `rbp_abi.h` | адреса и коды клавиш внутри `rbp` v1.20 (без нужды не трогать) |
| `test_flx4.c`, `test_grv6.c` + `tests/midi_*.dump` | тесты карт на MIDI, записанном с настоящих пультов |

Значения, которые удобно подстраивать без пересборки, лежат на Pi в
`/opt/rblive4/rb.local.conf` (все описаны в [scripts/device/rb.conf](../../scripts/device/rb.conf)):

| Ключ | |
|---|---|
| `RB_MIDI_MAP` | `auto` / `flx4` / `grv6` |
| `RB_JOG_PPR` | тиков джога на оборот (FLX4 720, GRV6 6449 — измерено) |
| `RB_JOG_SPEED_PCT` | GRV6: скорость джога, 180 = пластинка 33⅓ |
| `RB_JOG_LED_STEPS` | GRV6: делений индикатора в центре джога на оборот (72) |
| `RB_JOG_WINDOW_MS` | GRV6: окно усреднения скорости джога (8 мс) |
| `RB_MASTER_CUE_HW` | GRV6 сам сводит наушники (1) |

После правки `rb.local.conf` перезапустите плеер: `sudo systemctl restart rblive4`.

Комбинация «назад в меню» (FLX4 — SHIFT + SMART FADER, GRV6 — SHIFT + BACK,
держать 2 с) пишет `/tmp/rblaunch.request` для загрузочного меню, которое следит
за этим файлом. Меню в репозиторий не входит, поэтому без него комбинация ничего
не делает.

## Поменять маппинг

1. Правите `scripts/shims/map_flx4.c` или `map_grv6.c` (общее — `map_ux.c`).
2. Если меняется поведение, добавьте проверку в `test_flx4.c` / `test_grv6.c`.
3. `make shims`: сборка в Docker → `make check test` → шимы на Pi → перезапуск
   `rbp`. Новый тарбол и перезагрузка не нужны.
4. Проверяете на пульте. Прошлые шимы остаются на Pi как `/opt/rblive4/*.so.prev`.
5. Коммит.

## Снять MIDI органа управления

Цифры для карт **измеряют, а не угадывают**:

```sh
# на Pi, плеер остановлен (sudo systemctl stop rblive4)
aseqdump -l                       # найти клиент пульта
aseqdump -p <client>:0            # нажимать/крутить — ноты, CC, каналы
```

Для GRV6 есть готовые скрипты: `tools/grv6-capture.sh` (органы управления) и
`tools/grv6-ledprobe.sh` (перебор LED). `tools/aseqdump2dump.py` превращает
запись в фикстуру для тестов. Справочники MIDI — [docs/15-flx4-midi.md](../15-flx4-midi.md),
[docs/port/grv6-midi.md](../port/grv6-midi.md).

## Что сейчас в картах

- **FLX4:** карта upstream, поверх неё (ветка `pi5-flx4`) CUE/LOOP CALL, браузер
  с ручки, SMART CFX, LED Beat FX, SHIFT + джог = поиск, комбинация возврата, запрос
  позиций органов управления при старте.
- **GRV6:** деки 1/2, микшер, Beat FX, пэды, LED (PLAY, CUE, SYNC, loop, пэды,
  CH CUE, FX ON, метры, QUANTIZE), индикатор в центре джога, CFX на обоих каналах,
  комбинация возврата. Деки 3/4 ничего не делают: в `rbp` два плеера.
- **Не подтверждено:** плавность индикатора джога GRV6 на 100 Гц и сеты ≥ 1 ч на
  каждом пульте. [docs/port/grv6-midi.md](../port/grv6-midi.md) написан до правок
  2026-10-22/23; актуальное поведение — в коде и в `git log` ветки `pi5-flx4`.
