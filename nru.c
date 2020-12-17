#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define VAS 32 * 1024 * 1024 //virtual address space 2^25 Byte 
#define PAS 8 * 1024 * 1024 //physical address space 
#define SWAP 32 * 1024 * 1024 //swap file size
#define PAGE 4096
#define FRAME 4096

#define NUM_PAGE VAS / PAGE // 2^13
#define NUM_FRAME PAS / PAGE// 2^11

int MALLOC;
int FREE;
int OPERATION;

typedef struct _fnode{
	int vpn; //node ID
	struct _fnode *next;
	struct _fnode *prev;
} fnode;
fnode *head;
fnode *tail;

typedef struct {
    char valid;
    char present;
    char dirty;
	char nru;
	char usebit; //clock
    int alloc_size;
    int ref_addr; //pfn 
	int idx;   //vpn
	fnode* nptr; //LRU
} pte;

/*random nru*/
int workset[NUM_FRAME]; //vpn array 

char* _g_pm_start; //physical start address
int _g_swap_fd; //swap fd 
pte* _g_page_table; //page table

int _g_hits, _g_misses, _g_swap_R, _g_swap_W; //hit,miss,file io cnt

void init(int vm_size, int pm_size, int swap_size) {
    int i,res;
	
	for(i=0;i<NUM_FRAME;i++) workset[i] = -1;

    _g_pm_start = (char*)malloc(pm_size);
    _g_swap_fd = open("swap", O_RDWR | O_TRUNC | O_CREAT, 0644);
    if (_g_swap_fd < 0) {
        perror("Error opening file");
        exit(0);
    }
    res = lseek(_g_swap_fd, swap_size-1, SEEK_SET);
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
    /*vpn 연속적인 공간을 찾고 연속된 공간의 start vpn을 리턴, size=4096단위의 byte수*/
	int i,j;
	int pages = size / PAGE; // # of page to be allocated
	int cnt=0;

	for(i=0;i<NUM_PAGE;i++) {

		if(_g_page_table[i].valid == 0){
			
			for(j=0;j<pages; j++){
				
				//external fragment
				if(_g_page_table[i+j].valid == 1){
					i = i + j + 1;
					break;
				}
				//empty count 
				else
					cnt++;
			}
		}
		//empty count = empty pages
		if (cnt == pages)
			break;
	}
	//FULL Virtual
	if(i == NUM_PAGE)
		return -1;
	//NOT ENOUGH
	else
		return i;
}

int get_free_frame() {
	/*비어있는 frame 번호 리턴*/

	int pfn,pte;
	char arr[NUM_FRAME];
	memset(arr,0,NUM_FRAME);
	
	//probe occupied frame
	for(pte=0;pte<NUM_PAGE;pte++)
	{
		//pfn is occupied
		if(_g_page_table[pte].present == 1)
		{
			pfn = _g_page_table[pte].ref_addr;
			arr[pfn] = 1;
		}
	}
	
	//find empty frame
	for(pfn=0;pfn<NUM_FRAME;pfn++){
		
		if(arr[pfn] == 0)
			return pfn;
	}
	//arr[ALL pfn] = 1
    return -1;
}

int select_victim() {
	/*victim physical page frame 을 골라 vpn(pte) 리턴*/
	int vpn; //page table entry index
	int pfn;
	int i=0;

	//NRU
	for(i=0;i<NUM_PAGE;i++){
		if(_g_page_table[i].present == 1 && _g_page_table[i].nru == 1)
			return i;
	}

	//all blocks set to NRU=1
	for(i=0;i<NUM_PAGE;i++){
		_g_page_table[i].nru = 1;
	}

	//pick random 
	while(++i) {
		pfn = ((rand()+i) % (NUM_FRAME-1)) + 1;
		if(workset[pfn] != -1) 
			return workset[pfn];
	}

}

int eviction(int vpn) {
	/*vpn을 physical 메모리에서 쫓아냄, dirty일시 file에 적어야됨 RETURN empty physical frame num */
	int i;
	int nr,res; 
	int pfn = _g_page_table[vpn].ref_addr; // ex. vpn 3 map to pfn = 1 

	// Dirty page 
	if(_g_page_table[vpn].dirty == 1) {
		
		//WRITE to SWAP
		res = lseek(_g_swap_fd, vpn*PAGE , SEEK_SET); //pointer, at start address of 3rd page address
		if(res < 0){
			perror("Error swap lseek in eviction"); 
			exit(0);
		}
		nr = write(_g_swap_fd,&_g_pm_start[pfn*FRAME],FRAME); // read from 3rd page start address
		if(nr < 0){
			perror("Error swap write in eviction");
			exit(0);
		}
		
		//WRITE cnt
		_g_swap_W++;
	}

	// PTE update
	_g_page_table[vpn].present = 0;				//not in physical memory
	_g_page_table[vpn].dirty = 0;				//dirty clear
	_g_page_table[vpn].ref_addr = vpn;			//location in swap file


	//EMPTY pfn
	return pfn;
}


/* void select_victim()  int eviction(int vpn) */
void page_fault(int vpn) {
	/*physical memory에 없는 vpn이 파라미터로 주어지고, 해당 vpn을 physical memory에 올리는 함수 ALLOCATION or SWAPPING */
	int i;
	int nr,res; 
	int vpn_evict;
	int pfn_empty;
	char buf[PAGE];
	fnode *node;

	if(_g_page_table[vpn].present == 1){
		perror("vpn is in the physical memory already in page_fault");
		exit(0);
	}
	/* SWAPPING to PM  */
	else if(_g_page_table[vpn].valid == 1 && _g_page_table[vpn].present == 0) {
		
		/*READ swap */
		res = lseek(_g_swap_fd, vpn*PAGE, SEEK_SET);  
		if(res < 0){
			perror("lseek error in page_fault");
			exit(0);
		}
		nr = read(_g_swap_fd, buf, PAGE);
		if(nr < 0){
			perror("read error in page_fault");
			exit(0);
		}

		//READ cnt
		_g_swap_R++;

		/*WRITE PM*/
		
		//EVICTION
		if( get_free_frame() < 0 ) {

			//select victim
			vpn_evict = select_victim();
		
			//evict the page to swap 
			pfn_empty = eviction(vpn_evict);

		}
		//NOT FULL	
		else
			pfn_empty = get_free_frame();


		//write buf to page frame
		for(i=0;i<PAGE;i++){
			_g_pm_start[pfn_empty*FRAME + i] = buf[i];
		}

		//PTE update
		_g_page_table[vpn].present = 1;				//now in physical memory
		_g_page_table[vpn].dirty = 0;				//loading new
		_g_page_table[vpn].usebit = 1;				//page is referenced
		_g_page_table[vpn].idx = vpn;				//idx update
		_g_page_table[vpn].ref_addr = pfn_empty;	//PTE refer to physical memory 

		//WORKSET update
		workset[pfn_empty] = vpn;
	}
}

/*int get_free_frame()*/
void load_pages(int vpn, int num) { 
	/*해당 vpn부터 num개수 만큼의 페이지를 physical 메모리에 올림 = ALLOCATION*/
	int i,j;
	int pfn_empty;
	int vpn_evict;

	for(i=0;i<num;i++){
		/*ALLOCAION: vpn is not allocated */
		if(_g_page_table[vpn+i].valid == 0) {
			
			//EVICTION
			if( get_free_frame() < 0 ) {

				//select victim
				vpn_evict = select_victim();
			
				//evict the page to swap 
				pfn_empty = eviction(vpn_evict);

			}
			//NOT FULL	
			else
				pfn_empty = get_free_frame();
			
			//PTE update
			_g_page_table[vpn+i].nru = 0;				//ALLOCATION
			_g_page_table[vpn+i].valid = 1;				//ALLOCATION
			_g_page_table[vpn+i].present = 1;			//now in physical memory
			_g_page_table[vpn+i].dirty = 0;				//loading new
			_g_page_table[vpn+i].usebit = 1;			//page is referenced
			_g_page_table[vpn+i].idx = vpn+i;			//idx update
			_g_page_table[vpn+i].alloc_size = 0;
			_g_page_table[vpn+i].ref_addr = pfn_empty;	//PTE refer to physical memory 
			_g_page_table[vpn].alloc_size = num;

			//WORKSET update
			workset[pfn_empty] = vpn+i;
		}
	}
}
int mymalloc(int size) { 
	/*해당 size 만큼에 연속된 virtual address를 할당해줌고 첫번째 virtual address를 준다*/
	
	int vpn = get_free_pages(size);

	if (vpn < 0 )
		return -1;

	else {
		load_pages(vpn, size/PAGE);

	/*해당 크기만큼 연속된 공간이 없으면 -1출력, return -1*/
		return vpn * PAGE;
	}
}

void myfree(int vsa) {
   /*해당 address에 있는 할당된 페이지를 free*/
   /*해당 vsa가 valid가 아니거나 malloc했을때 그 주소값이 아니면 -1출력*/
}

void myset(int vsa, char value) {
    /*vsa에 한 바이트를 적음*/
	int vpn = vsa / PAGE;
	int vpo = vsa % PAGE;
	int pfn;
	int pfo = vpo; 
	int vpn_evict;
	fnode *node; //LRU

	/*vaild page가 아니면 -1 출력*/
	if(_g_page_table[vpn].valid != 1)
		printf("%d\n",-1);
	else
	{
		//page hit
		if(_g_page_table[vpn].present == 1)
		{
			pfn = _g_page_table[vpn].ref_addr;
			_g_pm_start[pfn*FRAME + pfo] = value;
	
			_g_page_table[vpn].dirty = 1;
			_g_page_table[vpn].usebit = 1;
			
			//cache hit
			_g_hits++;
		}
		//page fault
		else
		{	
			//SWAPPING
			page_fault(vpn);

			pfn = _g_page_table[vpn].ref_addr;
			_g_pm_start[pfn*FRAME + pfo] = value;
	
			_g_page_table[vpn].dirty = 1;
			_g_page_table[vpn].nru = 0;
			_g_page_table[vpn].usebit = 1;

			//cache miss
			_g_misses++;
		}
	}
}

char myget(int vsa) {
    /*vsa의 한 바이트를 리턴*/
	int vpn = vsa / PAGE;
	int vpo = vsa - vpn * PAGE;
	int pfn;
	int pfo = vpo; 
	int vpn_evict;
	char rtn;
	fnode *node; 

	/*vaild page가 아니면 -1 출력*/
	if(_g_page_table[vpn].valid != 1)
		printf("%d\n",-1);
	else
	{
		//page hit
		if(_g_page_table[vpn].present == 1)
		{
			pfn = _g_page_table[vpn].ref_addr;
			rtn = _g_pm_start[pfn*FRAME + pfo];

			_g_page_table[vpn].usebit = 1;

			//cache hit
			_g_hits++;
		}
		//page fault
		else
		{	
			//SWAPPING
			page_fault(vpn);

			pfn = _g_page_table[vpn].ref_addr;
			rtn = _g_pm_start[pfn*FRAME + pfo];
	
			_g_page_table[vpn].usebit = 1;
			_g_page_table[vpn].nru = 0;

			//cache miss
			_g_misses++;
		}
		return rtn;
	}
}

int main() {
    int *vsa;
    int size, idx;
	int addr;
    char value;
	char operation;
	char BUF[20];	

    init(VAS, PAS, SWAP);
	scanf("%d",&MALLOC);
	scanf("%d",&FREE);
	scanf("%d\n",&OPERATION);
	
    vsa = (int*)malloc(MALLOC * sizeof(int));

    for (int i=0; i<MALLOC; i++) {
        size = (rand() % 32 +1) * PAGE;
        vsa[i] = mymalloc(size);
		if(vsa[i] == -1) 
			printf("%d\n",vsa[i]);
    }

    for (int i=0; i<FREE; i++) {
        idx = rand() % MALLOC;
        size = 4 * PAGE;
        myfree(vsa[idx]);
        vsa[idx] = mymalloc(size);
    }
    
	for(int i=0; i<OPERATION; i++){
		/*parsing*/
		fgets(BUF, 20, stdin);
		operation = BUF[0];
		value = BUF[strlen(BUF)-2];
		strtok(BUF," ");
		addr = atoi(strtok(NULL," "));
		
		/*workload*/
		if(operation == 'S')
			myset(addr,value);
		if(operation == 'G')
			printf("%c\n",myget(addr));
	}
	
	//hit,miss file read, file write 순으로 출력
	printf("hit:%d miss:%d read:%d write:%d\n",_g_hits,_g_misses,_g_swap_R,_g_swap_W);	

	//Memory free
    free(vsa);
	free(head);
	free(tail);
	for(int i=0; i<NUM_PAGE; i++)
		if(_g_page_table[i].nptr != NULL) free(_g_page_table[i].nptr);
	free(_g_pm_start);
	free(_g_page_table);
	close(_g_swap_fd);
    return 0;
}

