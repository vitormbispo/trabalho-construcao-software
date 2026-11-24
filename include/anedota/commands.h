#ifndef ANEDOTA_COMMANDS_H
#define ANEDOTA_COMMANDS_H

int anedota_command_init(int argc, char **argv);
int anedota_command_hash_object(int argc, char **argv);
int anedota_command_cat_object(int argc, char **argv);
int anedota_command_write_tree(int argc, char **argv);
int anedota_command_commit(int argc, char **argv);
int anedota_command_log(int argc, char **argv);
int anedota_command_diff(int argc, char **argv);

#endif
