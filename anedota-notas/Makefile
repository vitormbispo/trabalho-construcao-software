.PHONY: anedota-lib build run test build-cgo run-cgo test-cgo clean

APP_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ADDR ?= 127.0.0.1:8080
NOTES_DIR ?= ./dados-notas
ANEDOTA_DIR ?= $(APP_DIR)/..

# Compila a biblioteca estática libanedota.a desacoplada da CLI
anedota-lib:
	$(MAKE) -C "$(ANEDOTA_DIR)" lib

# Modo padrão (Go puro compatível com o formato .anedota/)
build:
	cd "$(APP_DIR)" && go build -o bin/anedota-notas ./cmd/notas

run:
	cd "$(APP_DIR)" && go run ./cmd/notas -addr "$(ADDR)" -dir "$(NOTES_DIR)"

test:
	cd "$(APP_DIR)" && go test -v ./...

# Modo integrado via CGO vinculando diretamente à libanedota.a (sem invocar a CLI)
build-cgo: anedota-lib
	cd "$(APP_DIR)" && CGO_ENABLED=1 go build -tags anedota -o bin/anedota-notas-cgo ./cmd/notas

run-cgo: anedota-lib
	cd "$(APP_DIR)" && CGO_ENABLED=1 go run -tags anedota ./cmd/notas -addr "$(ADDR)" -dir "$(NOTES_DIR)"

test-cgo: anedota-lib
	cd "$(APP_DIR)" && CGO_ENABLED=1 go test -v -tags anedota ./...

clean:
	rm -rf "$(APP_DIR)/bin"
