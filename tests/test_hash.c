#include <anedota/hash.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *path = "test_hash_file.bin";

static void check_hash(const unsigned char *data, size_t size, const char *expected)
{
    pal_file_t *file = pal_open(path, "wb");
    assert(file != NULL);
    assert(pal_write(file, (const char *)data, size) == (int)size);
    assert(pal_close(file) == 0);

    char hash[ANEDOTA_SHA256_HEX_SIZE];
    for (int i = 0; i < 2; ++i)
    {
        file = pal_open(path, "rb");
        assert(file != NULL);
        assert(anedota_sha256_file(file, hash) == 0);
        assert(strcmp(hash, expected) == 0);
        assert(pal_close(file) == 0);
    }
    assert(unlink(path) == 0);
}

int main(void)
{
    const unsigned char empty[] = "";
    const unsigned char abc[] = "abc";
    const unsigned char binary[] = { 0, 1, 255, 'a', 'b', 'c', 0 };
    check_hash(empty, 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_hash(abc, 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_hash(binary, sizeof(binary), "9e8a7798d05322487ac8cfe4b88e15f6dc4a19d718b37b8675476f691468f534");

    static const struct { size_t size; const char *hash; } cases[] = {
        { 55, "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59" },
        { 56, "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562" },
        { 63, "29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488" },
        { 64, "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108" },
        { 65, "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781" },
        { 4097, "1e973d029df2b2c66cb42a942c5edb45966f02abaff29fe99410e44d271d0efc" }
    };
    unsigned char bytes[4097];
    for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (unsigned char)i;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        check_hash(bytes, cases[i].size, cases[i].hash);

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
    puts("Testes de SHA-256 concluídos com sucesso");
    return 0;
}
