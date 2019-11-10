#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include "ext2.h"

// File system command macros

#define MKDIR 0
#define CP 1
#define LN 2
#define RM 3
#define RESTORE 4
#define CHECKER 5

#define INODE_STRUCT 1
#define DIR_ENTRY_STRUCT 2
/* Define common helper functions to implement the various file system commands.
 * Implementations for these functions are found in helper.c
 */

int example_function();

int get_free_inode_number(unsigned char *disk);

struct ext2_dir_entry * path_walker(unsigned char *disk, char *path);

char * get_last_item_name(char *path);

int get_free_block_idx(unsigned char *disk);

int add_entry_in_new_block(unsigned char *disk, struct ext2_inode *parent_inode, unsigned char new_entry_file_type, int new_inode_number, int i_block_idx, char * new_entry_name, int free_block_number);

int add_entry_in_used_block(unsigned char *disk, struct ext2_inode *parent_inode, unsigned char new_entry_file_type, int new_inode_number, int i_block_idx, char *new_entry_name);

int check_space_available(unsigned char *disk, struct ext2_inode *parent_inode, int i_block_idx, char *new_entry_name);

int get_free_block_number(unsigned char *disk, struct ext2_inode *inode, int i_block_idx);

char * get_parent_directory_path(char *path);

int check_is_file(char *file);

int find_type(int type_mode, int struct_mode);

int update_inode_bitmap(unsigned char *disk, int inode_number);

int update_block_bitmap(unsigned char *disk, int block_number);

int get_inode_status(unsigned char *disk, int inode_number);

int get_block_status(unsigned char *disk, int block_number);

int flip_inode_bit(unsigned char *disk, int inode_number, int bit_status);

int flip_block_bit(unsigned char *disk, int block_number, int bit_status);

int get_free_block_number_simple(unsigned char *disk);
