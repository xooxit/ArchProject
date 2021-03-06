//
//  Project2_201611159_RANDOM.c
//  Project2
//
//  Created by Siwoo Chung on 6/18/18.
//  Copyright © 2018 Siwoo Chung. All rights reserved.
//


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <queue.h>

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

struct Node  // Node for doubly linked list (DLL)
{
    int data;
    struct Node *next;
    struct Node *prev;
};

struct Node* head = NULL; // DLL head
struct Node* tail = NULL; // DLL tail



void deleteNode(int vpn) { // Delete VPN node from DLL
    if (head->next->data == vpn) {  // A rarely used vpn is commonly deleted
        struct Node* del = head->next;
        head->next = del->next;
        del->next->prev = head;
        free(del);
        return;
    }
    struct Node* del = (struct Node*) malloc(sizeof(struct Node));
    del = head;
    while (1) {
        if (del == NULL)
            return;
        if (del->data == vpn)
            break;
        del = del->next;
    }
    del->prev->next = del->next;
    del->next->prev = del->prev;
    free(del);
    return;
}

int selectNthNode(int nth) { // Select N-th node and delete it from DLL
    struct Node* sel = (struct Node*) malloc(sizeof(struct Node));
    sel = head;
    int i;
    int vpn;
    for (i = 0; i < nth+1; i++) {
        if ((sel = sel->next) == tail) // if n-th node doesn't exist, return -1
            return -1;
    }
    vpn = sel->data;
    sel->prev->next = sel->next; // delete n-th node from DLL
    sel->next->prev = sel->prev;
    free(sel);
    return vpn;
}

void initDDL(struct Node **head_ref, struct Node **tail_ref) {  // Initialize DLL
    struct Node* beHead = (struct Node*) malloc(sizeof(struct Node));
    struct Node* beTail = (struct Node*) malloc(sizeof(struct Node));
    beHead->data = -1;
    beHead->prev = NULL;
    beHead->next = beTail;
    *head_ref = beHead;
    beTail->data = -1;
    beTail->prev = beHead;
    beTail->next = NULL;
    *tail_ref = beTail;
}

void push(int vpn) {  // Push VPN to DLL
    if (tail->prev->data == vpn) // A most recently used vpn is commonly pushed
        return;
    deleteNode(vpn);
    struct Node* new_node = (struct Node*) malloc(sizeof(struct Node));
    new_node->data = vpn;
    new_node->prev = tail->prev;
    new_node->prev->next = new_node;
    
    new_node->next = tail;
    tail->prev = new_node;
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
    off_t res;
    
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
    int i = -1;
    int j = 0;
    int numVPN = size / PAGE;
    while (1) {
        i++;
        int emptyNum = 0;
        if (i+numVPN >= NUM_PAGE+1) {// If VPN is overflowed, return -1
            //printf("Failed to malloc\n");
            return -1;
            
        }
        for (j = 0; j < numVPN; j++) {          // Repeat until find the contiguous pages
            if (!_g_page_table[i+j].valid) {
                emptyNum++;
            }
        }
        if (emptyNum == numVPN)                 // If find the contiguous pages, break loop
            break;
    }
    return i;
    
    /*vpn 연속적인 공간을 찾고 연속된 공간의 vpn을 리턴, size=4096단위의 byte수*/
    //return -1; //없으면 -1
}

int get_free_frame() {
    int i;
    for (i = 0; i < NUM_FRAME; i++) {   // Repeat until find empty frame
        if (physicalFrame[i] == 0) {    // If found an empty frame,
            physicalFrame[i] = 1;       // set it being used and return its frame number.
            return i;
        }
    }
    return -1;
    
    /*비어있는 frame 번호 리턴*/
    /*없으면 -1*/
}

int select_victim() {
    int victim = -1;
    int nth;
    while(1) {
        nth = rand() % NUM_FRAME;
        if ((victim = selectNthNode(nth)) >= 0) //Repeat until n-th node is exist
            break;
    }
    return victim;
}

void eviction(int vpn) {
    int i;
    ssize_t nbytes;
    char buf[FRAME];
    if (_g_page_table[vpn].dirty == 1) {    // if the page is dirty,
        for (i = 0; i < FRAME; i++) {       // buffering vpn for swap
            buf[i] = _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + i];
        }
        lseek(_g_swap_fd, (vpn) * PAGE, SEEK_SET);
        if ((nbytes = write(_g_swap_fd, buf, sizeof(buf))) < 0) { // Swapping
            perror("Eviction Failed.");
        }
        physicalFrame[_g_page_table[vpn].ref_addr] = 0; // Set frame as not using
        _g_page_table[vpn].ref_addr = vpn; // Set page table
        _g_page_table[vpn].present = 0;
        _g_page_table[vpn].dirty = 0;
        _g_swap_W++; //Mark as swap writing occured
    } else { // If not dirty, swap writing doesn't occur
        physicalFrame[_g_page_table[vpn].ref_addr] = 0; // Set frame as not using
        _g_page_table[vpn].ref_addr = vpn; // Set page table
        _g_page_table[vpn].present = 0;
        _g_page_table[vpn].dirty = 0;
    }
    
    /*vpn을 physical 메모리에서 쫓아냄 ,dirty일시 file에 적어야됨*/
    /*pwrite*/
    /*write, lseek*/
}

void page_fault(int vpn) {
    int i;
    ssize_t nbytes;
    char buf[FRAME];
    lseek(_g_swap_fd, vpn * PAGE, SEEK_SET);
    if ((nbytes = read(_g_swap_fd, buf, sizeof(buf))) < 0) { // Buffering vpn from swap
        perror("Page Fault Handling Failed.");
    }
    if ( (_g_page_table[vpn].ref_addr = get_free_frame()) < 0) { // If there's no emtpy frame
        int victim = select_victim(); // Do eviction
        eviction(victim);
        _g_page_table[vpn].ref_addr = get_free_frame(); // Again, allocate a frame
    }
    for (i = 0; i < FRAME; i++)
        _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + i] = buf[i]; // load vpn to physical memory
    _g_page_table[vpn].present = 1; // set page table
    _g_page_table[vpn].dirty = 0;
    _g_swap_R++; // Mark as swap reading occured
    
    /*physical memory에 없는 vpn이 파라미터로 주어지고, 해당 vpn을 pnysical memory에 올리는 함수*/
    /*pread*/
    /*read,lseek*/
}

void load_pages(int vpn, int num) {
    for (int i = 0; i < num; i++)
        page_fault(vpn + i);
    /*해당 vpn부터 num개수 만큼의 페이지를 physical 메모리에 올림*/
}

int mymalloc(int size) {
    int i;
    int vpn;
    int numVPN = size / PAGE;
    if ((vpn = get_free_pages(size)) < 0) { // If there's not enough pages
        printf("-1\n");                     // Print -1
        return -1;
    }
    for (i = 0; i < numVPN; i++) { // Set page table elements
        _g_page_table[vpn+i].valid = 1;
        _g_page_table[vpn+i].present = 1;
        _g_page_table[vpn+i].dirty = 1;
        if (i == 0)
            _g_page_table[vpn+i].alloc_size = numVPN;
        else
            _g_page_table[vpn+i].alloc_size = 0;
        if ( (_g_page_table[vpn+i].ref_addr = get_free_frame()) < 0) { // If there's no emtpy frame
            int victim = select_victim(); // Do eviction
            eviction(victim);
            _g_page_table[vpn+i].ref_addr = get_free_frame();  // Again, allocate a frame
        }
        push(vpn+i); // Push vpn to DLL
    }
    return vpn * PAGE;
    /*해당 size 만큼에 연속된 virtual address를 할당해줌고 첫번째 virtual address를 준다*/
    /*해당 크기만큼 연속된 공간이 없으면 -1출력, return -1*/
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
    for (i = 0; i < alloc_size; i++) { //Freeing vpn
        _g_page_table[vpn+i].valid = 0;
        if (_g_page_table[vpn+i].present == 1) {
            physicalFrame[_g_page_table[vpn].ref_addr] = 0;
        }
        _g_page_table[vpn+i].present = 0;
        _g_page_table[vpn+i].dirty = 0;
        _g_page_table[vpn+i].ref_addr = 0;
    }
    /*해당 address에 있는 할당된 페이지를 free*/
    /*해당 vsa가 valid가 아니거나 malloc했을때 그 주소값이 아니면 -1출력*/
}

int myset(int vsa, char value) {
    int vpn = vsa / PAGE;
    int offset = vsa % PAGE;
    if (_g_page_table[vpn].valid) { // If page is valid
        _g_page_table[vpn].dirty = 1; // Set page as dirty
        if (_g_page_table[vpn].present) { // If page is on physical mem
            _g_hits++;                    // Mark as hits
            _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset] = value;  // Set value
        } else {  // If page is not on physical mem
            _g_misses++;  // Mark as miss
            page_fault(vpn);  // Raise page fault
            _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset] = value; // Set value
        }
        push(vpn); // Push vpn to DLL
        return 0;
    } else { // If invalid page return -1
        printf("-1\n");
        return -1;
    }
    /*vsa에 한 바이트를 적음*/
    /*vaild page가 아니면 -1 출력*/
}

char myget(int vsa) {
    int vpn = vsa / PAGE;
    int offset = vsa % PAGE;
    if (_g_page_table[vpn].valid) {  // If page is valid
        push(vpn);  // Push vpn to DLL
        if (_g_page_table[vpn].present) { // If page is on physical mem
            _g_hits++; // Mark as hits
            return _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset]; //return value
        } else { // If page is not on physical mem
            _g_misses++; // Mark as miss
            page_fault(vpn); // Raise page fault
            return _g_pm_start[_g_page_table[vpn].ref_addr * FRAME + offset]; //return value
        }
    } else { // If invalid page return -1
        printf("-1\n");
        return -1;
    }
    /*vsa의 한 바이트를 리턴*/
    /*vaild page가 아니면 -1 출력*/
}


int main() {
    int *vsa;
    int size, idx;
    char value;
    
    init(VAS, PAS, SWAP);
    scanf("%d",&MALLOC);
    scanf("%d",&FREE);
    scanf("%d",&OPERATION);
    
    vsa = (int*)malloc(MALLOC * sizeof(int));
    
    initDDL(&head, &tail);
    
    
    for (int i=0; i<MALLOC; i++) {
        
        size = (rand() % 32+1) * PAGE;
        vsa[i] = mymalloc(size);
    }
    
    for (int i=0; i<FREE; i++) {
        idx = rand() % MALLOC;
        size = (rand() % 32+1) * PAGE;
        myfree(vsa[idx]);
        vsa[idx] = mymalloc(size);
    }
    
    for(int i=0; i<OPERATION; i++){
        scanf(" %c ", &value);
        if (value == 'S') {  // If input is starting with S
            int va;
            char input;
            scanf("%d", &va);
            scanf(" %c", &input);
            myset(va, input);  // Set value
        } else { // If input is starting with G
            int va;
            scanf("%d", &va);
            printf("%c\n", myget(va)); // Get value
        }
    }
    printf("%d %d %d %d\n", _g_hits, _g_misses, _g_swap_R, _g_swap_W);
    
    //hit,miss file read, file write 순으로 출력
    
    
    free(vsa);
    return 0;
}

