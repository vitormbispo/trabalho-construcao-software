#!/bin/sh
set -eu

cli=$1
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT HUP INT TERM
cd "$workspace"

expect_failure() {
    if "$cli" "$@" >stdout.txt 2>stderr.txt; then
        echo "Esperava falha: $*" >&2
        exit 1
    fi
    test -s stderr.txt
    test ! -s stdout.txt
}

"$cli" init >stdout.txt 2>stderr.txt
test -d .anedota/objetos
test -d .anedota/referencias/branches
grep -q 'Armazenamento ANEDOTA inicializado' stdout.txt
test ! -s stderr.txt
printf 'preservar\n' > .anedota/objetos/sentinel
"$cli" init >stdout.txt 2>stderr.txt
grep -q 'Armazenamento ANEDOTA inicializado' stdout.txt
test ! -s stderr.txt
test "$(cat .anedota/objetos/sentinel)" = 'preservar'

mkdir 'outro projeto'
"$cli" init 'outro projeto/' >stdout.txt
test -d 'outro projeto/.anedota/objetos'

printf 'abc' > 'arquivo de origem'
"$cli" hash-object 'arquivo de origem' >stdout.txt 2>stderr.txt
printf '%s\n' 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' >expected.txt
cmp stdout.txt expected.txt
test ! -s stderr.txt
cmp 'arquivo de origem' ".anedota/objetos/$(cat expected.txt)"
"$cli" hash-object 'arquivo de origem' >stdout.txt 2>stderr.txt
cmp stdout.txt expected.txt
test ! -s stderr.txt
expect_failure hash-object
expect_failure hash-object ''
expect_failure hash-object 'arquivo de origem' extra
expect_failure hash-object inexistente
mkdir sem_repositorio
(cd sem_repositorio && expect_failure hash-object ../'arquivo de origem')

expect_failure init extra excedente
test ! -e extra
expect_failure init ''
expect_failure init inexistente
test ! -e inexistente
expect_failure desconhecido
expect_failure --help extra

"$cli" >stdout.txt
grep -q 'COMMANDS:' stdout.txt
"$cli" --help >stdout.txt
grep -q 'init \[PATH\]' stdout.txt
grep -q 'hash-object FILE' stdout.txt
"$cli" -h >short.txt
cmp stdout.txt short.txt
"$cli" --version >stdout.txt
grep -q 'anedota 0.1.0' stdout.txt
grep -q 'BSD 3-Clause' stdout.txt
grep -q 'Escrito por' stdout.txt
for author in 'Vitor Bispo' 'Bruno Yozo' 'Julio Cesar' 'Gustavo Amaro'; do
    grep -q "$author" stdout.txt
done
"$cli" -v >short.txt
cmp stdout.txt short.txt

printf '[PASS] testes da CLI\n'
