.PHONY: build test installer
build:
	bash native/build.sh
installer: build
	python3 installer/package.py
test: installer
	bash tests/run.sh
