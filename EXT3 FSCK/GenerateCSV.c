#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "ext2_fs.h"

struct ext2_super_block superblock;
int filesystem;
__u32 __group_count, __bgdt_size, __block_bytesize;
unsigned long __blocks_per_1indir, __blocks_per_2indir;
off_t superblockOffset = 1024;
struct ext2_group_desc *bgdt;

enum __bitmap_type {BLOCK, INODE};
enum __indirect_type {DIRECTORY, REGFILE};

void handleCorruption(char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(0);
}

off_t blockToOff(int num)
{
	return superblockOffset + __block_bytesize * (num - 1);
}

__u32 getval(int type, int i, int j, int groupnum)
{
	__u32 r = 0;
	switch(type)
	{
		case BLOCK:
			r = ((__u32)groupnum * superblock.s_blocks_per_group) + ((i * 8) + (j + 1));
		break;
		case INODE:
			r = ((__u32)groupnum * superblock.s_inodes_per_group) + ((i * 8) + (j + 1));
		break;
	}
	return r;
} 

void scanFreeBitmap(__u8 *bitmap, __u32 len, int groupnum, int type)
{
	char *labels[] = {"BFREE\0", "IFREE\0", 0};
	__u32 bytes = len / 8 + 1;
	__u32 i, ctr = 1;
	for(i = 0; i < bytes; i++)
	{
		__u8 mask = 1, b = bitmap[i];
		__u32 j;
		//compare each bit in the byte
		for(j = 0; j < 8; j++)
		{
			if(ctr > len)
				return;
			if((~b) & mask)
				printf("%s,%u\n",
				labels[type],
				getval(type, i, j, groupnum));
			mask = mask << 1;
			ctr++;
		}
	}
}

char getfiletype(__u16 mode)
{
	if((mode & 0xA000) == 0xA000)
		return 's';
	if((mode & 0x8000) == 0x8000)
		return 'f';
	if((mode & 0x4000) == 0x4000)
		return 'd';
	return '?';
}

void yeetMyTimeFam(time_t sec, char *buf)
{
	strftime(buf, 20, "%m/%d/%y %H:%M:%S", gmtime(&sec));
}

void scanIndirects(int type, int indirectionLevel, __u32 parentInode, __u32 myBlockNum, unsigned long parentIndirectOffset)
{
	char block[__block_bytesize];
	//read in indirect block
	if((size_t)pread(filesystem, block, __block_bytesize, blockToOff(myBlockNum)) != __block_bytesize)
		handleCorruption("Indirect block corruption.");
	__u32 *blockref = (__u32 *)block, index_bound = __block_bytesize / sizeof(__u32), index;
	if(indirectionLevel == 1)
	{
		for(index = 0; index < index_bound; index++)
		{
			if(blockref[index])
			{
				printf("INDIRECT,%u,%d,%lu,%u,%u\n",
				parentInode,
				indirectionLevel,
				parentIndirectOffset + index,
				myBlockNum,
				blockref[index]);
				
				//Directory stuff
				if(type == DIRECTORY)
				{
					struct ext2_dir_entry *dirEntry;
					char block[__block_bytesize];
					//read in datablock
					if((size_t)pread(filesystem, block, __block_bytesize, blockToOff(blockref[index])) != __block_bytesize)
						handleCorruption("Directory i_block corruption.");
			
					//reset struct position
					off_t blockoff = 0;
					dirEntry = (struct ext2_dir_entry *)block;
			
					//parse block
					while(blockoff < __block_bytesize)
					{
						//if valid entry
						if(dirEntry->inode)
						{
							printf("DIRENT,%d,%ld,%d,%d,%d,'%s'\n",
							parentInode,
							(parentIndirectOffset + index) * __block_bytesize + blockoff,
							dirEntry->inode,
							dirEntry->rec_len,
							dirEntry->name_len,
							dirEntry->name);
						}
						
						//update offset
						blockoff += dirEntry->rec_len;
						dirEntry = (struct ext2_dir_entry *)((void *)block + blockoff);
					}
				}
			}
		}
	}
	else if(indirectionLevel == 2)
	{
		for(index = 0; index < index_bound; index++)
		{
			if(blockref[index])
			{
				printf("INDIRECT,%u,%d,%lu,%u,%u\n",
				parentInode,
				indirectionLevel,
				parentIndirectOffset + index * __blocks_per_1indir,
				myBlockNum,
				blockref[index]);
				scanIndirects(type, indirectionLevel - 1, parentInode, blockref[index], parentIndirectOffset + index * __blocks_per_1indir);
			}
		}
	}
	else if(indirectionLevel == 3)
	{
		for(index = 0; index < index_bound; index++)
		{
			if(blockref[index])
			{
				printf("INDIRECT,%u,%d,%lu,%u,%u\n",
				parentInode,
				indirectionLevel,
				parentIndirectOffset + index * __blocks_per_2indir,
				myBlockNum,
				blockref[index]);
				scanIndirects(type, indirectionLevel - 1, parentInode, blockref[index], parentIndirectOffset + index * __blocks_per_2indir);
			}
		}
	}
}

void scanDirentBlock(__u32 parentInode, struct ext2_inode inode)
{
	int dirblock;
	struct ext2_dir_entry *dirEntry;
	char block[__block_bytesize];
	//Direct blocks
	for(dirblock = 0; dirblock < 12; dirblock++)
	{
		//if i_block is in use
		if(inode.i_block[dirblock])
		{
			//read in i_block
			if((size_t)pread(filesystem, block, __block_bytesize, blockToOff(inode.i_block[dirblock])) != __block_bytesize)
				handleCorruption("Directory i_block corruption.");
			
			//reset struct position
			off_t blockoff = 0;
			dirEntry = (struct ext2_dir_entry *)block;
			
			//parse block
			while(blockoff < __block_bytesize)
			{
				//if valid entry
				if(dirEntry->inode)
				{
					printf("DIRENT,%d,%ld,%d,%d,%d,'%s'\n",
					parentInode,
					(dirblock * __block_bytesize) + blockoff,
					dirEntry->inode,
					dirEntry->rec_len,
					dirEntry->name_len,
					dirEntry->name);
				}
				
				//update offset
				blockoff += dirEntry->rec_len;
				dirEntry = (struct ext2_dir_entry *)((void *)block + blockoff);
			}
		}
	}
	//Indirect blocks for files and dirs use same parser
	if(inode.i_block[12])
		scanIndirects(DIRECTORY, 1, parentInode, inode.i_block[12], 12);
	if(inode.i_block[13])
		scanIndirects(DIRECTORY, 2, parentInode, inode.i_block[13], 12 + __blocks_per_1indir);
	if(inode.i_block[14])
		scanIndirects(DIRECTORY, 3, parentInode, inode.i_block[14], 12 + __blocks_per_1indir + __blocks_per_2indir);
}

void scanInodes(struct ext2_group_desc *bgd, __u32 groupnum)
{
	struct ext2_inode *__inode_table = (struct ext2_inode *)malloc(sizeof(struct ext2_inode) * superblock.s_inodes_per_group);
	//load Inode table
	if((size_t)pread(filesystem, __inode_table, sizeof(struct ext2_inode) * superblock.s_inodes_per_group, blockToOff(bgd->bg_inode_table)) != sizeof(struct ext2_inode) * superblock.s_inodes_per_group)
		handleCorruption("Inode table corruption.");
	char ft, ctime_buf[20], mtime_buf[20], atime_buf[20];
	
	//iterate table
	__u32 f, q;
	for(f = 0; f < superblock.s_inodes_per_group; f++)
	{
		//print allocated node
		if(__inode_table[f].i_mode && __inode_table[f].i_links_count)
		{
			yeetMyTimeFam((time_t)__inode_table[f].i_ctime, ctime_buf);
			yeetMyTimeFam((time_t)__inode_table[f].i_mtime, mtime_buf);
			yeetMyTimeFam((time_t)__inode_table[f].i_atime, atime_buf);
			ft = getfiletype(__inode_table[f].i_mode);
			//print csv entry
			printf("INODE,%d,%c,%o,%hu,%hu,%hu,%s,%s,%s,%d,%d",
			groupnum * superblock.s_inodes_per_group + f + 1,
			ft,
			__inode_table[f].i_mode & 0x0FFF,
			__inode_table[f].i_uid,
			__inode_table[f].i_gid,
			__inode_table[f].i_links_count,
			ctime_buf,
			mtime_buf,
			atime_buf,
			__inode_table[f].i_size,
			__inode_table[f].i_blocks);
			
			//print data block pointers
			if(ft == 'd' || ft == 'f' || (ft == 's' && __inode_table[f].i_size <= 60))
			{
				for(q = 0; q < 15; q++)
				{
					printf(",%d", __inode_table[f].i_block[q]);
				}
			}
			printf("\n");
			
			//scan directory entries
			if(ft == 'd')
				scanDirentBlock(groupnum * superblock.s_inodes_per_group + f + 1, __inode_table[f]);
			
			//scan indirect file blocks
			if(ft == 'f')
			{
				if(__inode_table[f].i_block[12])
					scanIndirects(REGFILE, 1, groupnum * superblock.s_inodes_per_group + f + 1, __inode_table[f].i_block[12], 12);
				if(__inode_table[f].i_block[13])
					scanIndirects(REGFILE, 2, groupnum * superblock.s_inodes_per_group + f + 1, __inode_table[f].i_block[13], 12 + __blocks_per_1indir);
				if(__inode_table[f].i_block[14])
					scanIndirects(REGFILE, 3, groupnum * superblock.s_inodes_per_group + f + 1, __inode_table[f].i_block[14], 12 + __blocks_per_1indir + __blocks_per_2indir);
			}
		}
	}

	free(__inode_table);
}

void scanBlockGroups()
{
	__group_count = 1 + (superblock.s_blocks_count - 1) / superblock.s_blocks_per_group;
	__bgdt_size = __group_count * sizeof(struct ext2_group_desc);
	bgdt = (struct ext2_group_desc *)malloc(__bgdt_size);
	
	//load block group descriptor table
	if(pread(filesystem, bgdt, __bgdt_size, blockToOff(2)) != __bgdt_size)
		handleCorruption("Block group descriptor table corrupted");
	
	__u8 __block_bitmap[__block_bytesize], __inode_bitmap[__block_bytesize];
	__u32 j, logicalBlocksRemaining = superblock.s_blocks_count;
	
	//scan each block group
	for(j = 0; j < __group_count; j++)
	{
		__u32 gBlocks = (logicalBlocksRemaining > superblock.s_blocks_per_group) ? superblock.s_blocks_per_group : logicalBlocksRemaining;
				
		//print group csv entry
		printf("GROUP,%d,%u,%u,%hu,%hu,%u,%u,%u\n",
		j,
		gBlocks,
		superblock.s_inodes_per_group,
		bgdt[j].bg_free_blocks_count,
		bgdt[j].bg_free_inodes_count,
		bgdt[j].bg_block_bitmap,
		bgdt[j].bg_inode_bitmap,
		bgdt[j].bg_inode_table);

		//load bitmaps
		if(pread(filesystem, __block_bitmap, __block_bytesize, blockToOff(bgdt[j].bg_block_bitmap)) != __block_bytesize)
			handleCorruption("Block bitmap corrupted.");
		if(pread(filesystem, __inode_bitmap, __block_bytesize, blockToOff(bgdt[j].bg_inode_bitmap)) != __block_bytesize)
			handleCorruption("Inode bitmap corrupted.");
		
		//find free stuff
		scanFreeBitmap(__block_bitmap, gBlocks, j, BLOCK);
		scanFreeBitmap(__inode_bitmap, superblock.s_inodes_per_group, j, INODE);	
		
		//scan inodes
		scanInodes(&bgdt[j], j);
		
		//update logical counters
		logicalBlocksRemaining -= superblock.s_blocks_per_group;
		if(logicalBlocksRemaining <= 0 && j + 1 < __group_count)
			handleCorruption("More groups that total available blocks permit.");
	}
	free(bgdt);
}

void loadSuperblock()
{
	//read and verify superblock
	if(pread(filesystem, &superblock, sizeof(superblock), superblockOffset) != 1024)
		handleCorruption("Superblock missing.");
	if(superblock.s_magic != EXT2_SUPER_MAGIC)
		handleCorruption("This isn't an EXT2 filesystem.");
	//calculate constants and print csv entry
	__block_bytesize = EXT2_MIN_BLOCK_SIZE << superblock.s_log_block_size;
	__blocks_per_1indir = __block_bytesize / sizeof(__u32);
	__blocks_per_2indir = __blocks_per_1indir * __block_bytesize / sizeof(__u32);
	if(__block_bytesize > EXT2_MAX_BLOCK_SIZE || __block_bytesize < EXT2_MIN_BLOCK_SIZE)
		handleCorruption("Block bytesize corrupted.");
	printf("SUPERBLOCK,%d,%d,%d,%hu,%d,%d,%d\n",
		superblock.s_blocks_count,
		superblock.s_inodes_count,
		__block_bytesize,
		superblock.s_inode_size,
		superblock.s_blocks_per_group,
		superblock.s_inodes_per_group,
		superblock.s_first_ino);
}

int main(int argc, char **argv)
{
	if(argc != 2)
	{
		fprintf(stderr, "Path to filesystem image required.\n");
		exit(1);
	}
	if((filesystem = open(argv[1], O_RDONLY)) < 0)
	{
		fprintf(stderr, "Failed to open: %s\n", argv[1]);
		exit(1);
	}
	loadSuperblock();
	scanBlockGroups();
	exit(0);
}