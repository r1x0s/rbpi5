# XDJ-RX3 rekordbox player on a Raspberry Pi 5 -- the one-command kit.
# Every target is a thin wrapper over pi5/scripts/; `make help` lists them.
.PHONY: help assets build shims pi-setup deploy deploy-shims doctor all check publish clean

help:
	@echo "make assets SRC=<dir>   prepare YOUR extracted XDJ-RX3 v1.20 tree -> extracted/"
	@echo "make build              DirectFB + shims + tests + chroot tarball (Docker) -> work/"
	@echo "make pi-setup           one-time Pi 5 setup from pi.env (kernel8.img, display, packages)"
	@echo "make deploy             install the player on the Pi"
	@echo "make all SRC=<dir>      assets + build + pi-setup + deploy"
	@echo "make shims              rebuild the shims (controller maps) and push them to the Pi"
	@echo "make doctor             read-only health check on the Pi"
	@echo "make check              before git push: no firmware, keys or binaries tracked"
	@echo "make publish GH_USER=<user> [PUBLIC=1]   push this repository to GitHub"

assets:
	@test -n "$(SRC)" || { echo "usage: make assets SRC=/path/to/XDJRX3"; exit 1; }
	sh pi5/scripts/prepare-assets.sh "$(SRC)"

build:
	sh pi5/scripts/build.sh $(if $(NO_TEST),--no-test)

pi-setup:
	sh pi5/scripts/pi-setup.sh

deploy:
	sh pi5/scripts/deploy.sh full

all:
	@test -n "$(SRC)" || test -f extracted/rbp-audio || { echo "usage: make all SRC=/path/to/XDJRX3"; exit 1; }
	@test -z "$(SRC)" || sh pi5/scripts/prepare-assets.sh "$(SRC)"
	sh pi5/scripts/build.sh
	sh pi5/scripts/pi-setup.sh
	sh pi5/scripts/deploy.sh full

shims:
	sh pi5/scripts/build.sh --shims $(if $(NO_TEST),--no-test)
	sh pi5/scripts/deploy.sh shims

deploy-shims:
	sh pi5/scripts/deploy.sh shims

doctor:
	sh pi5/scripts/deploy.sh doctor

check:
	sh pi5/scripts/check-git.sh

publish:
	@test -n "$(GH_USER)" || { echo "usage: make publish GH_USER=<github-user> [PUBLIC=1]"; exit 1; }
	sh pi5/scripts/publish-github.sh "$(GH_USER)" $(if $(PUBLIC),--public)

clean:
	rm -rf work/shims work/test.log work/shims.log work/chroot.log work/dfb-build.log
