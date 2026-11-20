#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <anedota/commands.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void test_init_new_and_existing_storage(void)
{
    char base[] = "/tmp/anedota-init-XXXXXX";
    assert(mkdtemp(base) != NULL);

    char root[sizeof(base) + sizeof("/.anedota")];
    char objects[sizeof(root) + sizeof("/objetos")];
    char references[sizeof(root) + sizeof("/referencias")];
    char branches[sizeof(references) + sizeof("/branches")];
    char sentinel[sizeof(objects) + sizeof("/sentinel")];
    snprintf(root, sizeof(root), "%s/.anedota", base);
    snprintf(objects, sizeof(objects), "%s/objetos", root);
    snprintf(references, sizeof(references), "%s/referencias", root);
    snprintf(branches, sizeof(branches), "%s/branches", references);
    snprintf(sentinel, sizeof(sentinel), "%s/sentinel", objects);

    char *args[] = {base};
    assert(anedota_command_init(1, args) == 0);
    assert(is_directory(root));
    assert(is_directory(objects));
    assert(is_directory(references));
    assert(is_directory(branches));

    FILE *file = fopen(sentinel, "wb");
    assert(file != NULL);
    assert(fwrite("preservar", 1, 9, file) == 9);
    assert(fclose(file) == 0);

    assert(anedota_command_init(1, args) == 0);
    file = fopen(sentinel, "rb");
    assert(file != NULL);
    char content[10] = {0};
    assert(fread(content, 1, sizeof(content), file) == 9);
    assert(strcmp(content, "preservar") == 0);
    assert(fclose(file) == 0);
    assert(is_directory(branches));

    assert(unlink(sentinel) == 0);
    assert(rmdir(branches) == 0);
    assert(rmdir(references) == 0);
    assert(rmdir(objects) == 0);
    assert(rmdir(root) == 0);
    assert(rmdir(base) == 0);
}

int main(void)
{
    test_init_new_and_existing_storage();
    puts("[PASS] test_command_init");
    return 0;
}
