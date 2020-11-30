#include <map>
#include <set>
#include <tuple>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>
#include <stdio.h>

using namespace std;

struct _superblock
{
	int numBlocks;
	int numInodes;
	int blockSize;
	int inodeSize;
	int blocksPerGroup;
	int inodesPerGroup;
	int firstNRInode;

};

enum StringValue {
	SUPERBLOCK,
	GROUP,
	BFREE,
	IFREE,
	INODE,
	DIRENT,
	INDIRECT
};

struct _datablockLink
{
	int inode;
	int offset;
	int lvl;
};

struct _dirent
{
	int parent;
	int inode;
	string name;
};

bool operator< (const struct _datablockLink& lhs, const struct _datablockLink& rhs)
{
	return (lhs.inode < rhs.inode) ? true : false;
}

bool operator< (const struct _dirent& lhs, const struct _dirent& rhs)
{
	return (lhs.inode < rhs.inode) ? true : false;
}

ifstream *csv;
int code = 0, __blocks_per_1indir, __blocks_per_2indir, __first_valid_datablock;
map<int, int> numInodeLinks;
map<int, int> inodeLinkCounts;
map<int, int> allegedParents;
map<int, set<int>> parentsThatClaimedChild;
map<int, set<struct _datablockLink>> linksToThisDatablock;
map<string, enum StringValue> csvType;
map<int, string> indirType;
set<int> iFree;
set<int> bFree;
set<int> iUsed;
set<int> bUsed;
set<int> unallocatedInodes;
set<struct _dirent> dirents;
struct _superblock superblock;

vector<string> split(const string &delimiter, const string &str)
{
	vector<string> arr;

	int strleng = str.length();
	int delleng = delimiter.length();
	if (delleng == 0)
		return arr;//no change

	int i = 0;
	int k = 0;
	while (i<strleng)
	{
		int j = 0;
		while (i + j<strleng && j<delleng && str[i + j] == delimiter[j])
			j++;
		if (j == delleng)//found delimiter
		{
			arr.push_back(str.substr(k, i - k));
			i += delleng;
			k = i;
		}
		else
		{
			i++;
		}
	}
	arr.push_back(str.substr(k, i - k));
	return arr;
}

void init()
{
	csvType["SUPERBLOCK"] = SUPERBLOCK;
	csvType["GROUP"] = GROUP;
	csvType["BFREE"] = BFREE;
	csvType["IFREE"] = IFREE;
	csvType["INODE"] = INODE;
	csvType["DIRENT"] = DIRENT;
	csvType["INDIRECT"] = INDIRECT;
	indirType[0] = "";
	indirType[1] = "INDIRECT ";
	indirType[2] = "DOUBLE INDIRECT ";
	indirType[3] = "TRIPLE INDIRECT ";
	string buffer;
	vector<string> fields;
	while (getline(*csv, buffer))
	{
		fields = split(",", buffer);
		switch (csvType[fields[0]])
		{
			case SUPERBLOCK:
				superblock.numBlocks = stoi(fields[1]);
				superblock.numInodes = stoi(fields[2]);
				superblock.blockSize = stoi(fields[3]);
				superblock.inodeSize = stoi(fields[4]);
				superblock.blocksPerGroup = stoi(fields[5]);
				superblock.inodesPerGroup = stoi(fields[6]);
				superblock.firstNRInode = stoi(fields[7]);
			break;
			case IFREE:
				iFree.insert(stoi(fields[1]));
			break;
			case BFREE:
				bFree.insert(stoi(fields[1]));
			break;
			case GROUP:
				__first_valid_datablock = stoi(fields[8]) + (int)ceil(superblock.inodeSize * superblock.numInodes / superblock.blockSize);
			break;
			case INODE:
			case DIRENT:
			case INDIRECT:
			default:
			break;
		}
	}
	__blocks_per_1indir = superblock.blockSize / 4;
	__blocks_per_2indir = __blocks_per_1indir * superblock.blockSize / 4;
	//rewind
	csv->clear();
	csv->seekg(0);
}

void checkBlockPointer(int parent, int blockNum, int offset, int lvl)
{
	if (blockNum < 0 || blockNum > superblock.numBlocks)
	{
		cout << "INVALID " << indirType[lvl] << "BLOCK " << blockNum << " IN INODE " << parent << " AT OFFSET " << offset << "\n";
		code = 2;
		return;
	}
	else if (blockNum > 0 && blockNum < __first_valid_datablock)
	{
		cout << "RESERVED " << indirType[lvl] << "BLOCK " << blockNum << " IN INODE " << parent << " AT OFFSET " << offset << "\n";
		code = 2;
		return;
	}
	//update bUsed and Datablock link map if its a valid datablock
	if (blockNum >= __first_valid_datablock)
	{
		bUsed.insert(blockNum);
		linksToThisDatablock[blockNum].insert({ parent, offset, lvl });
	}
	//check if allocated block is in freelist
	if (bFree.find(blockNum) != bFree.end())
	{
		cout << "ALLOCATED BLOCK " << blockNum << " ON FREELIST\n";
		code = 2;
	}
}

void pass1()
{
	string buffer;
	vector<string> fields;
	while(getline(*csv, buffer))
	{
		fields = split(",", buffer);
		switch (csvType[fields[0]])
		{
		case INODE:
			//only inodes in valid range examined
			if (stoi(fields[1]) <= superblock.numInodes)
			{
				if ((int)fields[2][0] != 0)
				{
					iUsed.insert(stoi(fields[1]));
					//allocated inode is on freelist
					if (iFree.find(stoi(fields[1])) != iFree.end())
					{
						cout << "ALLOCATED INODE " << fields[1] << " ON FREELIST\n";
						code = 2;
					}
					//ignore datablock checking for symbolic links 60 bytes or less
					if (fields[2].compare("s") == 0 && stoi(fields[10]) <= 60)
						break;
					//process direct pointers
					int ptr;
					for (int i = 0; i < 12; i++)
					{
						ptr = stoi(fields[12 + i]);
						checkBlockPointer(stoi(fields[1]), ptr, i, 0);
					}
					//process single indirect pointer
					ptr = stoi(fields[24]);
					checkBlockPointer(stoi(fields[1]), ptr, 12, 1);
					//process double indirect pointer
					ptr = stoi(fields[25]);
					checkBlockPointer(stoi(fields[1]), ptr, 12 + __blocks_per_1indir, 2);
					//process triple indirect pointer
					ptr = stoi(fields[26]);
					checkBlockPointer(stoi(fields[1]), ptr, 12 + __blocks_per_1indir + __blocks_per_2indir, 3);
				}
				else
				{
					unallocatedInodes.insert(stoi(fields[1]));
					//unallocated inode missing from freelist
					if (iFree.find(stoi(fields[1])) == iFree.end())
					{
						cout << "UNALLOCATED INODE " << fields[1] << " NOT ON FREELIST\n";
						code = 2;
					}
				}
				//log inode's declared hard link count
				inodeLinkCounts[stoi(fields[1])] = stoi(fields[6]);
				//printf("INODE\n");
			}
		break;
		case DIRENT:
			//invalid inode
			if (stoi(fields[3]) < 1 || stoi(fields[3]) > superblock.numInodes)
			{
				cout << "DIRECTORY INODE " << fields[1] << " NAME " << fields[6] << " INVALID INODE " << fields[3] << "\n";
				code = 2;
			}
			else
			{
				dirents.insert({ stoi(fields[1]), stoi(fields[3]), fields[6] });
			}
			//check '.'
			if (fields[6].compare("'.'") == 0 && fields[1].compare(fields[3]) != 0)
			{
				cout << "DIRECTORY INODE " << fields[1] << " NAME '.' LINK TO INODE " << fields[3] << " SHOULD BE " << fields[1] << "\n";
				code = 2;
			}
			//log '..''s alleged parent
			else if (fields[6].compare("'..'") == 0)
			{
				allegedParents[stoi(fields[1])] = stoi(fields[3]);
			}
			//log directory entry's declared child
			else
			{
				parentsThatClaimedChild[stoi(fields[3])].insert(stoi(fields[1]));
			}
			//update the inode this entry links to's total observed hard link count
			if (numInodeLinks.find(stoi(fields[3])) != numInodeLinks.end())
			{
				numInodeLinks[stoi(fields[3])] = numInodeLinks[stoi(fields[3])] + 1;
			}
			else
			{
				numInodeLinks[stoi(fields[3])] = 1;
			}
			//printf("DIRENT\n");
		break;
		case INDIRECT:
			switch (stoi(fields[2]))
			{
				case 1:
					checkBlockPointer(stoi(fields[1]), stoi(fields[5]), stoi(fields[3]), 0);
				break;
				case 2:
					checkBlockPointer(stoi(fields[1]), stoi(fields[5]), stoi(fields[3]), 1);
				break;
				case 3:
					checkBlockPointer(stoi(fields[1]), stoi(fields[5]), stoi(fields[3]), 2);
				break;
			}
			//printf("INDIRECT\n");
		break;
		default:
		break;
		}
	}
}

void pass2()
{
	//find mismatched '..' parentages (is that a word)
	map<int, int>::iterator it = allegedParents.begin();
	while (it != allegedParents.end())
	{
		//if the '..' doesn't correctly identify its parent, assume it knows who it's parent SHOULD be
		if (parentsThatClaimedChild[it->first].find(it->second) == parentsThatClaimedChild[it->first].end())
		{
			cout << "DIRECTORY INODE " << it->first << " NAME '..' LINK TO INODE " << it->second << " SHOULD BE " << *parentsThatClaimedChild[it->first].begin() << "\n";
			code = 2;
		}
		it++;
	}
	//Check linkcounts
	map<int, int>::iterator tmp;
	it = inodeLinkCounts.begin();
	while (it != inodeLinkCounts.end())
	{
		int observedLinks = 0;
		tmp = numInodeLinks.find(it->first);
		if (tmp != numInodeLinks.end())
			observedLinks = tmp->second;
		if (observedLinks != it->second)
		{
			cout << "INODE " << it->first << " HAS " << observedLinks << " LINKS BUT LINKCOUNT IS " << it->second << "\n";
			code = 2;
		}
		it++;
	}
	//check for unreferenced blocks
	for (int i = __first_valid_datablock; i < superblock.numBlocks; i++)
	{
		if (bFree.find(i) == bFree.end() && bUsed.find(i) == bUsed.end())
		{
			cout << "UNREFERENCED BLOCK " << i << "\n";
			code = 2;
		}
	}
	//check for unreferenced inodes
	for (int i = superblock.firstNRInode; i < superblock.numInodes; i++)
	{
		if (iFree.find(i) == iFree.end() && iUsed.find(i) == iUsed.end())
		{
			cout << "UNALLOCATED INODE " << i << " NOT ON FREELIST\n";
			code = 2;
		}
	}
	//check duplicate datablock links
	map<int, set<struct _datablockLink>>::iterator iit = linksToThisDatablock.begin();
	set<struct _datablockLink>::iterator junk;
	while (iit != linksToThisDatablock.end())
	{
		if (iit->second.size() > 1)
		{
			//print an error for each reference involved in the collision
			junk = iit->second.begin();
			while (junk != iit->second.end())
			{
				cout << "DUPLICATE " << indirType[junk->lvl] << "BLOCK " << iit->first << " IN INODE " << junk->inode << " AT OFFSET " << junk->offset << "\n";
				code = 2;
				junk++;
			}
		}
		iit++;
	}
	//check unallocated inodes directory references for references in valid range
	set<struct _dirent>::iterator q = dirents.begin();
	while (q != dirents.end())
	{
		if (unallocatedInodes.find(q->inode) != unallocatedInodes.end() || (iFree.find(q->inode) != iFree.end() && iUsed.find(q->inode) == iUsed.end()))
		{
			cout << "DIRECTORY INODE " << q->parent << " NAME " << q->name << " UNALLOCATED INODE " << q->inode << "\n";
			code = 2;
		}
		q++;
	}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		fprintf(stderr, "ERROR: Requires single argument of type filepath.\n");
		exit(1);
	}
	csv = new ifstream(argv[1], ios::binary);
	if (!csv->is_open())
	{
		fprintf(stderr, "ERROR: Failed to open file: %s.\n", argv[1]);
		exit(1);
	}
	init();
	pass1();
	pass2();
	csv->close();
	delete csv;
	exit(code);
}