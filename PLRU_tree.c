#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define VAS 32 * 1024 * 1024 //virtual address space
#define PAS 8 * 1024 * 1024 //physical address space 
#define SWAP 32 * 1024 * 1024 //swap file size
#define PAGE 4096
#define FRAME 4096

#define NUM_PAGE VAS / PAGE 
#define NUM_FRAME PAS / PAGE

int MALLOC;
int FREE;
int OPERATION;

// Binary Tree for PLRU algorithm.
typedef struct TreeNode {
	bool is_leaf;
	int vpn; //don't care if is_leaf is false
	bool dir_left;
	unsigned int left_idx;
	unsigned int right_idx;
} TreeNode;

TreeNode* root = NULL;

TreeNode* initTree(TreeNode* root, int num_PAGE)
{
	int num_node = 2 * num_PAGE - 1;
	root = (TreeNode*)malloc(sizeof(TreeNode) * num_node);

	for (int idx = 0; idx < num_node; ++idx) {
		if (idx - num_PAGE + 1 >= 0) {
			root[idx].is_leaf = true;
			root[idx].vpn = 0;
			root[idx].dir_left = true;
			root[idx].left_idx = -1;
			root[idx].right_idx = -1;
		}
		else {
			root[idx].is_leaf = false;
			root[idx].vpn = -1;
			root[idx].dir_left = true;
			root[idx].left_idx = 2 * idx + 1;
			root[idx].right_idx = 2 * idx + 2;
		}
	}

	return root;
}

void freeTree(TreeNode* root)
{
	free(root);
}

void insertTree(TreeNode* root, int vpn)
{
	int idx = 0; // search from root
	printf("inserting vpn %d... path:0", vpn);
	while (root[idx].is_leaf != true) {
		if (root[idx].dir_left == true) {
			int new_idx = root[idx].left_idx;
			root[idx].dir_left = !root[idx].dir_left;
			idx = new_idx;
		}
		else {
			int new_idx = root[idx].right_idx;
			root[idx].dir_left = !root[idx].dir_left;
			idx = new_idx;
		}
		printf("->%d", idx);
	}

	printf("\n");
	root[idx].vpn = vpn;
}

int traverseTree(TreeNode* root)
{
	int idx = 0;
	while (root[idx].is_leaf != true) {
		if (root[idx].dir_left == true) {
			int new_idx = root[idx].left_idx;
			root[idx].dir_left = !root[idx].dir_left;
			idx = new_idx;
		}
		else {
			int new_idx = root[idx].right_idx;
			root[idx].dir_left = !root[idx].dir_left;
			idx = new_idx;
		}
	}

	int victim = root[idx].vpn;
	root[idx].vpn = -1;
	return victim;
}

typedef struct {
	char valid;
	char present;
	char dirty;
	int alloc_size;
	int ref_addr;
} pte;

char* _g_pm_start; //physical start address
int _g_swap_fd; //swap fd
pte* _g_page_table; //page table

int _g_hits, _g_misses, _g_swap_R, _g_swap_W; //hit,miss,file io cnt
int physicalFrame[2048];

void init(int vm_size, int pm_size, int swap_size) {
	int res;

	_g_pm_start = (char*)malloc(pm_size);
	_g_swap_fd = open("swap", O_RDWR | O_TRUNC | O_CREAT, 0644);

	if (_g_swap_fd < 0) {
		perror("Error opening file");
		exit(0);
	}
	res = lseek(_g_swap_fd, swap_size - 1, SEEK_SET);
	if (res < 0) {
		close(_g_swap_fd);
		perror("Error calling lseek() to stretch the file");
		exit(0);
	}

	_g_page_table = (pte*)malloc(NUM_PAGE * sizeof(pte));
	memset(_g_page_table, 0, NUM_PAGE * sizeof(pte));

	_g_hits = _g_misses = _g_swap_R = _g_swap_W = 0;

	return;
}


int get_free_pages(int size) {
	int i = -1;
	int j = 0;
	int numVPN = size / PAGE;
	while (1) {
		i++;
		int emptyNum = 0;
		if (i + numVPN >= NUM_PAGE + 1)
			return -1;
		for (j = 0; j < numVPN; j++) {
			if (_g_page_table[i + j].valid == 0) {
				emptyNum++;
			}
		}
		if (emptyNum == numVPN)
			break;
	}
	return i;
}

int get_free_frame() {
	int i;
	for (i = 0; i < NUM_FRAME; i++) {
		if (physicalFrame[i] == 0) {
			physicalFrame[i] = 1;
			return i;
		}
	}
	return -1;
}

int select_victim() {
	int victim;
	victim = traverseTree(root);
	return victim;
}

void eviction(int vpn) {
	int i;
	ssize_t nbytes;
	char buf[FRAME];
	if (_g_page_table[vpn].dirty == 1) {
		for (i = 0; i < FRAME; i++) {
			buf[i] = _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + i];
		}
		lseek(_g_swap_fd, (vpn)*PAGE, SEEK_SET);
		if ((nbytes = write(_g_swap_fd, buf, sizeof(buf))) < 0) {
			perror("Eviction Failed.");
		}
		physicalFrame[_g_page_table[vpn].ref_addr] = 0;
		_g_page_table[vpn].ref_addr = vpn;
		_g_page_table[vpn].present = 0;
		_g_page_table[vpn].dirty = 0;
		_g_swap_W++;
	}
	else {
		physicalFrame[_g_page_table[vpn].ref_addr] = 0;
		_g_page_table[vpn].ref_addr = vpn;
		_g_page_table[vpn].present = 0;
		_g_page_table[vpn].dirty = 0;
	}
}

void page_fault(int vpn) {
	int i;
	ssize_t nbytes;
	char buf[FRAME];
	lseek(_g_swap_fd, vpn * PAGE, SEEK_SET);
	if ((nbytes = read(_g_swap_fd, buf, sizeof(buf))) < 0) {
		perror("Page Fault Handling Failed.");
	}
	if ((_g_page_table[vpn].ref_addr = get_free_frame()) < 0) {
		int victim = select_victim();
		eviction(victim);
		_g_page_table[vpn].ref_addr = get_free_frame();
	}
	for (i = 0; i < FRAME; i++)
		_g_pm_start[_g_page_table[vpn].ref_addr * FRAME + i] = buf[i];
	_g_page_table[vpn].present = 1;
	_g_page_table[vpn].dirty = 0;
	_g_swap_R++;
}

void load_pages(int vpn, int num) {
	for (int i = 0; i < num; i++)
		page_fault(vpn + i);
}

int mymalloc(int size) {
	int i;
	int vpn;
	int numVPN = size / PAGE;
	if ((vpn = get_free_pages(size)) < 0) {
		printf("-1\n");
		return -1;
	}
	for (i = 0; i < numVPN; i++) {
		_g_page_table[vpn + i].valid = 1;
		_g_page_table[vpn + i].present = 1;
		_g_page_table[vpn + i].dirty = 1;
		if (i == 0)
			_g_page_table[vpn + i].alloc_size = numVPN;
		else
			_g_page_table[vpn + i].alloc_size = 0;
		if ((_g_page_table[vpn + i].ref_addr = get_free_frame()) < 0) {
			int victim = select_victim();
			eviction(victim);
			_g_page_table[vpn + i].ref_addr = get_free_frame();
		}
		insertTree(root, vpn + i);
	}
	return vpn * PAGE;
}

void myfree(int vsa) {
	int i;
	int alloc_size;
	int vpn;
	vpn = vsa / PAGE;
	if (vpn < 0 || vpn > NUM_PAGE || _g_page_table[vpn].valid == 0) {
		printf("-1\n");
		return;
	}
	alloc_size = _g_page_table[vpn].alloc_size;
	for (i = 0; i < alloc_size; i++) {
		_g_page_table[vpn + i].valid = 0;
		if (_g_page_table[vpn + i].present == 1) {
			physicalFrame[_g_page_table[vpn].ref_addr] = 0;
		}
		_g_page_table[vpn + i].present = 0;
		_g_page_table[vpn + i].dirty = 0;
		_g_page_table[vpn + i].ref_addr = 0;
	}
}

int myset(int vsa, char value) {
	int vpn = vsa / PAGE;
	int offset = vsa % PAGE;
	if (_g_page_table[vpn].valid) {
		_g_page_table[vpn].dirty = 1;
		if (_g_page_table[vpn].present) {
			_g_hits++;
			_g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset] = value;
		}
		else {
			_g_misses++;
			page_fault(vpn);
			_g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset] = value;
		}
		insertTree(root, vpn);
		push(vpn);
		return 0;
	}
	else {
		printf("-1\n");
		return -1;
	}
}

char myget(int vsa) {
	int vpn = vsa / PAGE;
	int offset = vsa % PAGE;
	if (_g_page_table[vpn].valid) {
		insertTree(root, vpn);
		push(vpn);
		if (_g_page_table[vpn].present) {
			_g_hits++;
			return _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset];
		}
		else {
			_g_misses++;
			page_fault(vpn);
			return _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset];
		}
	}
	else {
		printf("-1\n");
		return -1;
	}
}

int main() {
	int* vsa;
	int size, idx;
	char value;

	init(VAS, PAS, SWAP);
	scanf("%d", &MALLOC);
	scanf("%d", &FREE);
	scanf("%d", &OPERATION);

	vsa = (int*)malloc(MALLOC * sizeof(int));

	initTree(root, NUM_PAGE);

	for (int i = 0; i < MALLOC; i++) {
		size = (rand() % 32 + 1) * PAGE;
		vsa[i] = mymalloc(size);
	}

	for (int i = 0; i < FREE; i++) {
		idx = rand() % MALLOC;
		size = (rand() % 32 + 1) * PAGE;
		myfree(vsa[idx]);
		vsa[idx] = mymalloc(size);
	}

	for (int i = 0; i < OPERATION; i++) {
		scanf(" %c ", &value);
		if (value == 'S') {
			int va;
			char input;
			scanf("%d", &va);
			scanf(" %c", &input);
			myset(va, input);
		}
		else {
			int va;
			scanf("%d", &va);
			printf("%c\n", myget(va));
		}
	}
	printf("%d %d %d %d\n", _g_hits, _g_misses, _g_swap_R, _g_swap_W);

	freeTree(root);
	return 0;
}
