#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "ext2.h"
#include "helper.h"
#include <string.h>

unsigned char *disk;
int total_count = 0;

int main(int argc, char **argv) {

  if(argc != 2) {
      fprintf(stderr, "Usage: <image file name>\n");
      exit(1);
  }
  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
      perror("mmap");
      exit(1);
  }

  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
  struct ext2_inode *inode_table = (struct ext2_inode *)(disk +
    EXT2_BLOCK_SIZE * gd->bg_inode_table);
  struct ext2_inode *root = (struct ext2_inode *)(disk
    + gd->bg_inode_table * EXT2_BLOCK_SIZE + sizeof(struct ext2_inode));
  unsigned char *block_bits = (unsigned char *)(disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE);
  unsigned char *inode_bits = (unsigned char *)(disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE);

  //for each task, count the fix

  /* taks 1: check superblock and block group counters:
   * loop through bitmaps to update free inode and free data blocks */
  int sb_block_count = 0;
  int sb_inode_count = 0;
  int bg_block_count = 0;
  int bg_inode_count = 0;
  int used_block_count = 0;
  int free_block_count = 0;
  int used_inode_count = 0;
  int free_inode_count = 0;
  char *superblock = "superblock";
  char *blockgroup = "block group";
  char *freeblock = "free blocks";
  char *freeinode = "free inodes";


	//iteration through block bitmap
	for (int byte = 0; byte < sb->s_blocks_count / 8; byte++) {
		for(int bit = 0; bit < 8; bit++) {
			if (block_bits[byte] & (1 << bit)) { //bitmap is 1
				used_block_count ++;
			} else { //bitmap is 0
				free_block_count ++;
			}
		}
	}

  //iterate through inode bitmap
 	for (int byte = 0; byte < sb->s_inodes_count / 8; byte++) {
		for(int bit = 0; bit < 8; bit++) {
			if (inode_bits[byte] & (1 << bit)) {
		    	used_inode_count ++;
		  } else {
				free_inode_count ++;
		  }
		}
 	}

 	sb_block_count = abs(sb->s_free_blocks_count - free_block_count);
 	sb_inode_count = abs(sb->s_free_inodes_count - free_inode_count);
 	bg_block_count = abs(gd->bg_free_blocks_count - free_block_count);
 	bg_inode_count = abs(gd->bg_free_inodes_count - free_inode_count);
 	printf("Fixed: %s's %s counter was off by %d compared to the bitmap\n", superblock, freeblock, sb_block_count);
 	printf("Fixed: %s's %s counter was off by %d compared to the bitmap\n", superblock, freeinode, sb_inode_count);
 	printf("Fixed: %s's %s counter was off by %d compared to the bitmap\n", blockgroup, freeblock, bg_block_count);
 	printf("Fixed: %s's %s counter was off by %d compared to the bitmap\n", blockgroup, freeinode, bg_inode_count);

 	//update count
  	sb->s_free_inodes_count = free_inode_count;
 	sb->s_free_blocks_count = free_block_count;
 	gd->bg_free_inodes_count = free_inode_count;
 	gd->bg_free_blocks_count = free_block_count;


  	//task 2 to task 5
 	struct ext2_inode *cur_inode = root;
 	int fix_count = 0;
 	//loop through inode table
	for(int i = 0; i < used_inode_count; i++) {
		if (cur_inode->i_size == 0) {
	        continue;
	    }
		//if its directory 
		if(cur_inode->i_mode & EXT2_S_IFDIR){
			
			//loop through iblock
			for (int j = 0; j < 12; j++) { //update for iblock
				if (cur_inode->i_block[j] == 0) {
	        		continue;
	      		}
	      		struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(disk + cur_inode->i_block[j] * EXT2_BLOCK_SIZE);
	      		int rec_len_sum = 0;
			    while(rec_len_sum < 1024) { //update for rec_len
			      	
			      	//update file type for entry
			      	/* task 2: check if file/directory/symlink has the correct type
   					* loop through all files and change/update file_type to inode's i_mode */
			      	if(find_type(entry->file_type, DIR_ENTRY_STRUCT) != find_type(cur_inode->i_mode, INODE_STRUCT)){
	      				entry->file_type = find_type(cur_inode->i_mode, INODE_STRUCT);
	      				printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", entry->inode);
	      				fix_count ++;
	      			}

	      			/* task 3: check if inodes map inode bitmap
   					* loops through all files and update the inode bitmap (according to file) 
   					* and also update counter in super block and block group*/
	      			//update inode bitmap
	      			int update_inode = update_inode_bitmap(disk, entry->inode);
	      			if(update_inode){
	      				printf("Fixed: inode [%d] not marked as in-use\n", entry->inode);
	      				fix_count ++;
	      			}
	      			
	      			/* task 4: check if inode's i_dtime is 0
   					* reset i_dtime to 0 to indicate file should not be marked for removal */
	      			//update i_dtime
	      			if(cur_inode->i_dtime != 0){
	      				cur_inode->i_dtime = 0;
	      				printf("Fixed: valid inode marked for deletion: [%d]\n", entry->inode);
	      				fix_count ++;
	      			}

	      			/* task 5: check if data blocks map data bitmap
   					* loops through all data blocks to update the data bitmap*/
	      			//find the inode for each file and loop through the iblock of that inode to update block bitmap
	      			struct ext2_inode *file_inode = &inode_table[entry->inode - 1];
	      			int fix_block_num = 0;
	      			for (int k=0; k<12; k++){
	      				if (file_inode->i_block[k] == 0) {
			        		continue;
			      		}
	      				int block_number = file_inode->i_block[k];
	      				int update_block = update_block_bitmap(disk, block_number);
		      			if(update_block){
		      				fix_block_num ++;
		      			}
	      			}
	      			fix_count += fix_block_num;
	      			printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", fix_block_num, entry->inode);
	      			
	      			rec_len_sum = rec_len_sum + entry->rec_len;
	      			entry = (struct ext2_dir_entry *)(disk + 1024 * cur_inode->i_block[j] + rec_len_sum);
			    }

			}
		}
		//if it's not a directory, go to next inode
		cur_inode =(struct ext2_inode *) cur_inode + sizeof(struct ext2_inode);
	}

 	total_count = sb_block_count + sb_inode_count + bg_block_count + bg_inode_count + fix_count;
 	if(total_count){
 		printf("%d file system inconsistencies repaired!\n", total_count);
 	} else {
 		printf("No file system inconsistencies detected!\n");
 	}
 	
}