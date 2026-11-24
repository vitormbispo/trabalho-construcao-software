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
printf 'abc' > 'outro nome.txt'
"$cli" hash-object 'outro nome.txt' >stdout.txt 2>stderr.txt
cmp stdout.txt expected.txt
test ! -s stderr.txt
test "$(find .anedota/objetos -mindepth 1 -maxdepth 1 ! -name sentinel | wc -l | tr -d ' ')" = '1'
"$cli" cat-object "$(cat expected.txt)" >stdout.txt 2>stderr.txt
cmp stdout.txt 'arquivo de origem'
test ! -s stderr.txt

printf '\000\001\377abc\000' > 'arquivo binario'
"$cli" hash-object 'arquivo binario' >binary_hash.txt 2>stderr.txt
"$cli" cat-object "$(cat binary_hash.txt)" >stdout.txt 2>stderr.txt
cmp stdout.txt 'arquivo binario'
test ! -s stderr.txt

expect_failure hash-object
expect_failure hash-object ''
expect_failure hash-object 'arquivo de origem' extra
expect_failure hash-object inexistente
expect_failure cat-object
expect_failure cat-object ''
expect_failure cat-object "$(printf '%064d' 0)"
expect_failure cat-object "$(cat expected.txt)" extra
mkdir sem_repositorio
(cd sem_repositorio && expect_failure hash-object ../'arquivo de origem')
(cd sem_repositorio && expect_failure cat-object "$(cat ../expected.txt)")
(cd sem_repositorio && expect_failure write-tree)
(cd sem_repositorio && expect_failure commit -m 'sem repo')

mkdir -p arvore_a/sub/profundo arvore_a/vazio
printf 'raiz\n' > arvore_a/raiz.txt
printf 'sub\n' > arvore_a/sub/interno.txt
printf 'folha\n' > arvore_a/sub/profundo/folha.txt
(cd arvore_a && "$cli" init >stdout.txt 2>stderr.txt)
rm arvore_a/stdout.txt arvore_a/stderr.txt
(cd arvore_a && "$cli" write-tree >../tree_hash_1.txt 2>../stderr.txt)
test ! -s stderr.txt
test "$(wc -c < tree_hash_1.txt | tr -d ' ')" = '65'
test -f "arvore_a/.anedota/objetos/$(cat tree_hash_1.txt)"
(cd arvore_a && "$cli" write-tree >../tree_hash_2.txt 2>../stderr.txt)
test ! -s stderr.txt
cmp tree_hash_1.txt tree_hash_2.txt

mkdir -p arvore_b/sub/profundo arvore_b/vazio
printf 'folha\n' > arvore_b/sub/profundo/folha.txt
printf 'sub\n' > arvore_b/sub/interno.txt
printf 'raiz\n' > arvore_b/raiz.txt
"$cli" write-tree arvore_b >tree_hash_b.txt 2>stderr.txt
test ! -s stderr.txt
cmp tree_hash_1.txt tree_hash_b.txt

"$cli" write-tree arvore_b/sub >subtree_hash.txt 2>stderr.txt
test ! -s stderr.txt
test -f ".anedota/objetos/$(cat subtree_hash.txt)"
test "$(cat subtree_hash.txt)" != "$(cat tree_hash_b.txt)"

expect_failure write-tree ''
expect_failure write-tree arvore_b extra
expect_failure write-tree inexistente
expect_failure write-tree 'arquivo de origem'

mkdir repo_commit
printf 'v1\n' > repo_commit/arquivo.txt
(cd repo_commit && "$cli" init >stdout.txt 2>stderr.txt)
rm repo_commit/stdout.txt repo_commit/stderr.txt
(cd repo_commit && "$cli" commit -m 'Commit inicial' >../commit_1.txt 2>../stderr.txt)
test ! -s stderr.txt
test "$(wc -c < commit_1.txt | tr -d ' ')" = '65'
test -f "repo_commit/.anedota/objetos/$(cat commit_1.txt)"
cmp commit_1.txt repo_commit/.anedota/referencias/branches/main
test "$(cat repo_commit/.anedota/HEAD)" = 'ref: referencias/branches/main'

printf 'v2\n' > repo_commit/arquivo.txt
mkdir repo_commit/sub
printf 'novo\n' > repo_commit/sub/item.txt
(cd repo_commit && "$cli" commit -m 'Segundo commit' -a 'Vitor' >../commit_2.txt 2>../stderr.txt)
test ! -s stderr.txt
test "$(wc -c < commit_2.txt | tr -d ' ')" = '65'
test "$(cat commit_1.txt)" != "$(cat commit_2.txt)"
test -f "repo_commit/.anedota/objetos/$(cat commit_2.txt)"
cmp commit_2.txt repo_commit/.anedota/referencias/branches/main

(cd repo_commit && expect_failure commit)
(cd repo_commit && expect_failure commit -m)
(cd repo_commit && expect_failure commit -m '')
(cd repo_commit && expect_failure commit -m 'msg' -a)
(cd repo_commit && expect_failure commit -m 'msg' -a '')
(cd repo_commit && expect_failure commit -m 'msg' -m 'dup')
(cd repo_commit && expect_failure commit -m 'msg' extra)

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
grep -q 'cat-object HASH' stdout.txt
grep -q 'write-tree \[PATH\]' stdout.txt
grep -q 'commit -m MESSAGE \[-a AUTHOR\]' stdout.txt
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
