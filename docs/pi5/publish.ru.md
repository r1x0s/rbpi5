# Опубликовать свою копию на GitHub

[English](publish.md) · **Русский**

```sh
make check                                  # 1. в git нет того, что должно остаться у вас
make publish GH_USER=<github-логин>         # 2. приватный репозиторий rbpi5
make publish GH_USER=<github-логин> PUBLIC=1 #   или публичный
```

- С [GitHub CLI](https://cli.github.com/) (после `gh auth login`) репозиторий
  создаётся сам. Без него скрипт попросит сначала создать **пустой** репозиторий
  на <https://github.com/new> (без README, `.gitignore` и лицензии).
- Текущая ветка (`pi5-flx4`) пушится как `main` по SSH (`PUBLISH_HTTPS=1` — по
  HTTPS) в remote `github`. `origin` остаётся upstream, так что `git fetch origin`
  по-прежнему приносит новое из rbpi4b.
- Пушится только закоммиченное; о незакоммиченных правках `make check`
  предупредит.
- Своё имя: `REPO_NAME=<имя> make publish …`.

## Что проверяет `make check`

Все файлы, отслеживаемые в `HEAD`:

- **ошибка** — имена файлов прошивки, ключей и бинарников (`*.UPD`, `*.cramfs`,
  `*.key`, `*.so`, `rbp`, …), ELF-бинарники, файлы больше 2 МБ, приватные ключи и
  содержимое любого файла из `SECRET_FILES` (печатается только имя файла, где оно
  нашлось):
  ```sh
  SECRET_FILES="/path/to/a/secret" make check
  ```
- **предупреждение** — частные IP-адреса (часто забытый адрес Pi; в upstream
  `docs/18-prodjlink.md` есть примеры).

## Подтягивать upstream

```sh
git fetch origin
git merge origin/main          # README.md — этого форка; README upstream — README-rbpi4b.md
make build && make check
```
