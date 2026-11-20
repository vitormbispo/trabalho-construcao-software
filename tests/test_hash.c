#include <anedota/hash.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static const char *path = "test_hash_file.bin";

static void write_file(const char *file_path, const unsigned char *data, size_t size)
{
    pal_file_t *file = pal_open(file_path, "wb");
    assert(file != NULL);
    if (size > 0)
        assert(pal_write(file, (const char *)data, size) == (int)size);
    assert(pal_close(file) == 0);
}

static void hash_file(const char *file_path, char output[ANEDOTA_SHA256_HEX_SIZE])
{
    pal_file_t *file = pal_open(file_path, "rb");
    assert(file != NULL);
    assert(anedota_sha256_file(file, output) == 0);
    assert(pal_close(file) == 0);
}

static void check_hash(const unsigned char *data, size_t size, const char *expected)
{
    write_file(path, data, size);

    char hash[ANEDOTA_SHA256_HEX_SIZE];
    for (int i = 0; i < 2; ++i)
    {
        hash_file(path, hash);
        assert(strcmp(hash, expected) == 0);
    }
    assert(unlink(path) == 0);
}

static void test_known_vectors(void)
{
    /* Vetores conhecidos do SHA-256 (NIST FIPS 180-4 / RFC 6234) */
    static const struct { const char *input; const char *expected; } vectors[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        {
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
        },
        {
            "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
            "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"
        }
    };

    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i)
        check_hash((const unsigned char *)vectors[i].input,
                   strlen(vectors[i].input), vectors[i].expected);

    const unsigned char binary[] = { 0, 1, 255, 'a', 'b', 'c', 0 };
    check_hash(binary, sizeof(binary),
               "9e8a7798d05322487ac8cfe4b88e15f6dc4a19d718b37b8675476f691468f534");

    static const struct { size_t size; const char *hash; } boundaries[] = {
        { 55, "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59" },
        { 56, "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562" },
        { 63, "29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488" },
        { 64, "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108" },
        { 65, "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781" },
        { 4097, "1e973d029df2b2c66cb42a942c5edb45966f02abaff29fe99410e44d271d0efc" }
    };
    unsigned char bytes[4097];
    for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (unsigned char)i;
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i)
        check_hash(bytes, boundaries[i].size, boundaries[i].hash);
}

static void test_metadata_independence(void)
{
    const unsigned char content[] = "abc";
    const char *file_a = "test_hash_nome_a.txt";
    const char *file_b = "test_hash_outro_nome.bin";

    write_file(file_a, content, 3);
    write_file(file_b, content, 3);

    /* Datas e permissões diferentes nos dois arquivos */
    struct utimbuf time_a = { 1000000000, 1000000000 };
    struct utimbuf time_b = { 1700000000, 1700000000 };
    assert(utime(file_a, &time_a) == 0);
    assert(utime(file_b, &time_b) == 0);
    assert(chmod(file_a, 0644) == 0);
    assert(chmod(file_b, 0400) == 0);

    struct stat st_a, st_b;
    assert(stat(file_a, &st_a) == 0 && stat(file_b, &st_b) == 0);
    assert(st_a.st_mtime != st_b.st_mtime);
    assert((st_a.st_mode & 0777) != (st_b.st_mode & 0777));

    char hash_a[ANEDOTA_SHA256_HEX_SIZE];
    char hash_b[ANEDOTA_SHA256_HEX_SIZE];
    hash_file(file_a, hash_a);
    hash_file(file_b, hash_b);
    assert(strcmp(hash_a, hash_b) == 0);

    /* Alterar permissão novamente (executável) também não muda o hash */
    assert(chmod(file_b, 0755) == 0);
    hash_file(file_b, hash_b);
    assert(strcmp(hash_a, hash_b) == 0);

    assert(unlink(file_a) == 0);
    assert(unlink(file_b) == 0);
}

static void test_invalid_arguments(void)
{
    char hash[ANEDOTA_SHA256_HEX_SIZE] = "unchanged";
    assert(anedota_sha256_file(NULL, hash) == -1);
    assert(strcmp(hash, "unchanged") == 0);

    pal_file_t *file = pal_open(path, "wb");
    assert(file != NULL);
    assert(anedota_sha256_file(file, NULL) == -1);
    assert(anedota_sha256_file(file, hash) == -1);
    assert(strcmp(hash, "unchanged") == 0);
    assert(pal_close(file) == 0);
    assert(unlink(path) == 0);
}

int main(void)
{
    test_known_vectors();
    test_metadata_independence();
    test_invalid_arguments();
    puts("Testes de SHA-256 concluídos com sucesso");
    return 0;
}
