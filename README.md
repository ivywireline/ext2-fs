# ext2-fs

- ext2_cp: This program takes three command line arguments. The first is the name of an ext2
formatted virtual disk. The second is the path to a file on your native operating system, and the third
is an absolute path on your ext2 formatted disk. The program should work like cp, copying the file
on your native file system onto the specified location on the disk. If the specified file or target
location does not exist, then your program should return the appropriate error (ENOENT). Please
read the specifications of ext2 carefully, some things you will not need to worry about (like
permissions, gid, uid, etc.), while setting other information in the inodes may be important (e.g.,
i_dtime).

- ext2_mkdir: This program takes two command line arguments. The first is the name of an ext2
formatted virtual disk. The second is an absolute path on your ext2 formatted disk. The program
should work like mkdir, creating the final directory on the specified path on the disk. If any
component on the path to the location where the final directory is to be created does not exist or if
the specified directory already exists, then your program should return the appropriate error
(ENOENT or EEXIST). Again, please read the specifications to make sure you're implementing
everything correctly (e.g., directory entries should be aligned to 4B, entry names are not nullterminated, etc.).

- ext2_ln: This program takes three command line arguments. The first is the name of an ext2
formatted virtual disk. The other two are absolute paths on your ext2 formatted disk. The program
should work like ln, creating a link from the first specified file to the second specified path. This
program should handle any exceptional circumstances, for example: if the source file does not exist
(ENOENT), if the link name already exists (EEXIST), if a hardlink refers to a directory (EISDIR), etc.
then your program should return the appropriate error code. Additionally, this command may take a
"-s" flag, after the disk image argument. When this flag is used, your program must create a symlink
instead (other arguments remain the same). If in doubt about correct operation of links, use the ext2
specs and ask on the discussion board.

- ext2_rm: This program takes two command line arguments. The first is the name of an ext2
formatted virtual disk, and the second is an absolute path to a file or link (not a directory) on that
disk. The program should work like rm, removing the specified file from the disk. If the file does not
exist or if it is a directory, then your program should return the appropriate error. Once again, please
read the specifications of ext2 carefully, to figure out what needs to actually happen when a file or
link is removed (e.g., no need to zero out data blocks, must set i_dtime in the inode, removing a
directory entry need not shift the directory entries after the one being deleted, etc.).


- ext2_restore: This program takes two command line arguments. The first is the name of an ext2
formatted virtual disk, and the second is an absolute path to a file or link (not a directory!) on that
disk. The program should be the exact opposite of rm, restoring the specified file that has been
previous removed. If the file does not exist (it may have been overwritten), or if it is a directory, then
your program should return the appropriate error.
Hint: The file to be restored will not appear in the directory entries of the parent directory, unless you
search the "gaps" left when files get removed. The directory entry structure is the key to finding out
these gaps and searching for the removed file.
Note: If the directory entry for the file has not been overwritten, you will still need to make sure that
the inode has not been reused, and that none of its data blocks have been reallocated. You may
assume that the bitmaps are reliable indicators of such fact. If the file cannot be fully restored, your
program should terminate with ENOENT, indicating that the operation was unsuccessful.
Note(2): For testing, you should focus primarily on restoring files that you've removed using your
ext2_rm implementation, since ext2_restore should undo the exact changes made by ext2_rm.
While there are some removed entries already present in some of the image files provided, the
respective files have been removed on a non-ext2 file system, which is not doing the removal the
same way that ext2 would. In ext2, when you do "rm", the inode's i_blocks do not get zeroed, and
you can do full recovery, as stated in the assignment (which deals solely with ext2 images, hence
why you only have to worry about this type of (simpler) recovery). In other FSs things work
differently. In ext3, when you rm a file, the data block indexes from its inode do get zeroed, so
recovery is not as trivial. For example, there are some removed files in deletedfile.img, which
have their blocks zero-ed out (due to how these images were created). In such cases, your code
should still work, but simply recover a file as an empty file (with no data blocks). However, for the
most part, try to recover files that you've ext2_rm-ed yourself, to make sure that you can restore
data blocks as well.
Note(3): We will not try to recover files that had hardlinks at the time of removal. This is because
when trying to restore a file, if its inode is already in use, there are two options: the file we're trying
to restore previously had other hardlinks (and hence its inode never really got invalidated), _or_ its
inode has been re-allocated to a completely new file. Since there is no way to tell between these 2
possibilities, recovery in this case should not be attempted.
BONUS: Implement an additional "-r" flag (after the disk image argument), which allows restoring
directories as well. In this case, you will have to recursively restore all the contents of the directory
specified in the last argument. If "-r" is used with a regular file or link, then it should be ignored (the
restore operation should be carried out as if the flag had not been entered). If you decide to do the
bonus, make sure first that your ext2_restore works, then create a new copy of it and rename it to
ext2_restore_bonus.c, and implement the additional functionality in this separate source file.

- ext2_checker: This program takes only one command line argument: the name of an ext2
formatted virtual disk. The program should implement a lightweight file system checker, which
detects a small subset of possible file system inconsistencies and takes appropriate actions to fix
them (as well as counts the number of fixes), as follows:
a. the superblock and block group counters for free blocks and free inodes must match the
number of free inodes and data blocks as indicated in the respective bitmaps. If an
inconsistency is detected, the checker will trust the bitmaps and update the counters. Once
such an inconsistency is fixed, your program should output the following message: "Fixed:
X's Y counter was off by Z compared to the bitmap", where X stands for either "superblock"
or "block group", Y is either "free blocks" or "free inodes", and Z is the difference (in absolute
value). The Z values should be added to the total number of fixes.
b. for each file, directory, or symlink, you must check if its inode's i_mode matches the directory
entry file_type. If it does not, then you shall trust the inode's i_mode and fix the file_type to
match. Once such an inconsistency is repaired, your program should output the following
message: "Fixed: Entry type vs inode mismatch: inode [I]", where I is the inode number for the
respective file system object. Each inconsistency counts towards to total number of fixes.
c. for each file, directory or symlink, you must check that its inode is marked as allocated in the
inode bitmap. If it isn't, then the inode bitmap must be updated to indicate that the inode is in
use. You should also update the corresponding counters in the block group and superblock
(they should be consistent with the bitmap at this point). Once such an inconsistency is
repaired, your program should output the following message: "Fixed: inode [I] not marked as
in-use", where I is the inode number. Each inconsistency counts towards to total number of
fixes.
d. for each file, directory, or symlink, you must check that its inode's i_dtime is set to 0. If it isn't,
you must reset (to 0), to indicate that the file should not be marked for removal. Once such an
inconsistency is repaired, your program should output the following message: "Fixed: valid
inode marked for deletion: [I]", where I is the inode number. Each inconsistency counts
towards to total number of fixes.
e. for each file, directory, or symlink, you must check that all its data blocks are allocated in the
data bitmap. If any of its blocks is not allocated, you must fix this by updating the data
bitmap. You should also update the corresponding counters in the block group and
superblock, (they should be consistent with the bitmap at this point). Once such an
inconsistency is fixed, your program should output the following message: "Fixed: D in-use
data blocks not marked in data bitmap for inode: [I]", where D is the number of data blocks
fixed, and I is the inode number. Each inconsistency counts towards to total number of fixes.
