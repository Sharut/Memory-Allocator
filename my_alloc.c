#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>


// These are the sizes of used abstract variables
// Header_t size = 8
// Node_t size = 16
// Node_t* size = 8;
typedef struct header_t { // Allocated chunk 
    int allocated_size;
    int magic;
    long long extra_size;  // This extra size is added sp that sizeof(header_t)== sizeof(node_t), this is needed
                    // since if the sizes are unequal, then on free(), the pointers might get over-written.
} header_t;


typedef struct node_t {  // Node of free list 
    int size; 
    struct node_t *next;
} node_t;


typedef struct heap_info{ // Heap Information structure
    int blocks_allocated;
    int max_chunk;
    int min_chunk;
    int curr_size;
}heap_info;



// Initialising all pointers to NULL
node_t *head = NULL;
node_t *start_address = NULL;
heap_info *all_heap_info = NULL;


int my_init(){
    start_address = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    head = (node_t*)((char*)(start_address) + sizeof(heap_info));
    all_heap_info = (heap_info*) start_address;

    if (head == MAP_FAILED) {
        perror("MMAP Failed");
        return -1;
    }

    head->size = 4096 - sizeof(node_t) - sizeof(heap_info);
    head->next = NULL;
    all_heap_info->max_chunk = head->size;
    all_heap_info->min_chunk = head->size;
    all_heap_info->curr_size = sizeof(node_t); //Must include node_t + header_t + allocated space
    all_heap_info->blocks_allocated = 0;
    return 0;
}


// Function to iterate over the free list and return the maximum chunk size
int get_max_chunk() {
    int max_chunk = -1;
    node_t* counter = head;
    while(counter) {
        if (counter->size > max_chunk) 
            max_chunk = counter->size;
        counter=counter->next;
    }
    return max_chunk;
}

// Function to iterate over the free list and return the minimum chunk size
int get_min_chunk() {
    int flag=0;
    int min_chunk = 100000;
    node_t* counter = head;
    while(counter) {
        if (counter->size != 0) flag=1;
        counter = counter->next;
    }

    // If the entire free list has zero sized elements, only then return 0
    if(!flag) return 0;

    counter = head;
    while(counter) {
        // Else return non zero smallest size in free list
        if (counter->size < min_chunk && counter->size!=0) 
            min_chunk = counter->size;
        counter=counter->next;
    }
    return min_chunk;
}


// Function to update heap info during my_alloc() execution
void update_info_alloc(int previous_size){ // previous_Size is the size of freelist_node which is to be split
	if(all_heap_info->max_chunk == previous_size)  all_heap_info->max_chunk = get_max_chunk();	
	
    // ALWAYS Traverse for changing smallest chunk
    all_heap_info->min_chunk = get_min_chunk();
	return;

}

//Function to update heap info during my_free() execution
void update_info_free(int old_size, int new_size, int new_curr_size){ 

    // Update max chunk only if size of coalesced block is more than  max chunk size
    if (new_size > all_heap_info->max_chunk) 
        all_heap_info->max_chunk = new_size;

    // Iterate through the free list only when freed node size corresponded to the min chunk size 
    if ((old_size == all_heap_info->min_chunk) || old_size==0 || all_heap_info->min_chunk == 0){ 
        all_heap_info->min_chunk = get_min_chunk();
    }

    // Update current size to new current size 
    all_heap_info->curr_size = new_curr_size;
    return;

}

// Function to find previous node of a given node in the free list
node_t * find_previous(node_t* current){
	node_t *counter = head;
    while(counter){
        if(counter->next == current) return counter;
        counter=counter->next;
    }
    return NULL;
}

// Function to implement the first fit technique. 
node_t *FirstFit(int size) {
    node_t *counter = head;
    while(counter){
        if(counter->size >= size) {
            return counter;
        }
        if (counter->size - size < 0 && size - counter->size <= sizeof(node_t)) {
            return counter;
        }
        counter = counter->next;
    }
    return NULL;
}


// Function to replace the given node with a new node
void update_free_list(node_t *node, int allocated_size) { 
    node_t *new_node = (node_t*)((char*)(node) + allocated_size);
    node_t *cur_prev = find_previous(node);
    node_t *cur_next = node->next;
    if (node->size - allocated_size > 0) {
        new_node->size = node->size - allocated_size;
        if (cur_prev)
            cur_prev->next = new_node;
        new_node->next = cur_next;
    }
    else {
        if (cur_prev)
            cur_prev->next = cur_next;
    }
    if(!cur_prev) {
        if (node->size - allocated_size > 0) {
            head = new_node;
        }
        else
            head = NULL;
    }
}


// Function performing "malloc()" as in C language
void* my_alloc(int count){
    if (count <= 0 || count % 8 || !head) {
        return NULL;
    }
    int required_size = count + sizeof(header_t);
    node_t *free_block = FirstFit(required_size);
    

    if(free_block){
        int size = free_block->size;
        update_free_list(free_block, required_size);
        header_t* header = (header_t*)free_block;

        if(free_block->size - required_size < 0 && required_size - free_block->size <= sizeof(node_t))
	        header->allocated_size = free_block->size + sizeof(node_t) - sizeof(header_t);
	    else {
	    	header->allocated_size = count;
	    }
        header->magic = 1234567;
        all_heap_info->blocks_allocated += 1;

        if(head){
            all_heap_info->curr_size += header->allocated_size + sizeof(header_t);
            update_info_alloc(size);// Update the heap info structures. 
        }

        else{
            all_heap_info->max_chunk = 0;
            all_heap_info->min_chunk = 0;
            all_heap_info->curr_size = 4096 - sizeof(heap_info);
        }
        
        return (void*)((char*)(free_block) + sizeof(header_t));
    }

    return NULL;
}



// Function to merge the freed node with the free node below it 
node_t* coalesce_from_below(node_t *ptr, int freed_size){ // Free_size doesn't include header_size since it is occupied
    node_t *counter = head;
    while(counter){
        if( (node_t *)((char*)(counter) + counter->size + sizeof(node_t)) == (node_t *)((char*)ptr - sizeof(header_t))  ) { //Coalesce from below
            counter->size += (freed_size + sizeof(header_t));
            return counter;           
        }
        counter = counter->next;
    }
    return NULL;   
}


// Function to merge the freed node with the free node from above 
int coalesce_from_top(node_t *ptr, node_t* below_ptr, int freed_size){ // Free_size doesn't include header_size since it is occupied
    node_t *counter = head;
    node_t * above_ptr = NULL;
    while(counter){
        if((node_t *)((char*)ptr + freed_size) == (node_t *)counter) { 
            above_ptr = counter;
            break;
        }
        counter = counter->next;
    }

    if(!below_ptr && !above_ptr) return 0; 

    if(below_ptr && !above_ptr){
        int old_size = below_ptr->size - freed_size - sizeof(header_t);
        
        update_info_free(old_size, below_ptr->size, all_heap_info->curr_size - (freed_size + sizeof(header_t)));
        return 1;
    }

    
    node_t* temp = find_previous(above_ptr);
    node_t* temp_above = above_ptr->next;

    if(below_ptr && above_ptr){
        int old_size = below_ptr->size - freed_size - sizeof(header_t);
        below_ptr->size += above_ptr->size + sizeof(node_t);
        
        // Delete above_ptr
        if (temp)
            temp->next = temp_above;
        else 
            head = temp_above;
        update_info_free(old_size, below_ptr->size, all_heap_info->curr_size - (freed_size + sizeof(header_t) + sizeof(node_t)));

    }
    else{    
        node_t* temp_new = (node_t*)((char*)ptr - sizeof(header_t) );
        temp_new->size = freed_size + above_ptr->size + sizeof(header_t);
        temp_new->next = above_ptr->next;
         // Deleting above_ptr and replacing my temp_new
        if(temp)   
            temp->next = temp_new;
        else
            head = temp_new; 

        update_info_free(above_ptr->size, temp_new->size, all_heap_info->curr_size - (freed_size + sizeof(header_t)));
 
    }
    return 1;
}

// Function that implements free() of C
void my_free(void* ptr){
    ptr = (node_t *)ptr;
    if(!ptr) return;
    header_t* header_ptr = (header_t*)((char*)ptr - sizeof(header_t));

    int size = header_ptr->allocated_size;

    if(header_ptr->magic != 1234567){ // It was already a free block
        // printf("Segmentation fault: The given pointer was never allocated!");
        return;
    } 

    node_t* coalesced_below_ptr = coalesce_from_below(ptr, size);
    int coalesced_above_ptr = coalesce_from_top(ptr, coalesced_below_ptr, size);
    


    if(!coalesced_below_ptr & !coalesced_above_ptr){ // add a new node to free list
        node_t* final_node = (node_t*)((char*)ptr - sizeof(header_t));
        final_node->size = sizeof(header_t) + size - sizeof(node_t);

        if(head){
        	node_t* head_next = head->next;
	        final_node->next = head_next;
	        head->next = final_node;
        }
        else{
        	head = final_node;

        }

        // Update Heap Info - No coalesce, so no traversal !
        if (final_node->size > all_heap_info->max_chunk)
            all_heap_info->max_chunk = final_node->size;

        if (final_node->size!=0 && final_node->size < all_heap_info->min_chunk) 
            all_heap_info->min_chunk = final_node->size;

        if(all_heap_info->min_chunk == 0) all_heap_info->min_chunk = get_min_chunk();

        all_heap_info->curr_size = all_heap_info->curr_size - (sizeof(header_t) + size - sizeof(node_t));      

    }

    // Update blocks allocated for all cases
    all_heap_info->blocks_allocated -= 1;
    return;
}

// Function to clean the allocated memory
void my_clean(){
    head = NULL;
    int val = munmap(start_address, 4096);
    if(val==-1){
       // printf("Unsuccessful unmap!\n");
       return; 
    }
    // printf("Successful unmap!\n");
    return;

}

// prints heap information
void my_heapinfo(){
    int a, b, c, d, e, f;
    printf("=== Heap Info ================\n");
    printf("Max Size: %ld\n", 4096 - sizeof(heap_info));
    printf("Current Size: %d\n", all_heap_info->curr_size);
    printf("Free Memory: %ld\n", 4096 - sizeof(heap_info) - all_heap_info->curr_size);
    printf("Blocks allocated: %d\n", all_heap_info->blocks_allocated);
    printf("Smallest available chunk: %d\n", all_heap_info->min_chunk);
    printf("Largest available chunk: %d\n", all_heap_info->max_chunk);
    printf("==============================\n");
    return;
}

// Iterates over the free list, to print sizes of free list
void print_free_list(){
    node_t *counter = head;
    while(counter){
        printf("%d->", counter->size );
        counter = counter->next;
    }
    printf("\n");
    my_heapinfo();
}


// int main(){
//     my_init();
//     int* node1 = (int*)my_alloc(4040);
//     print_free_list();
//     int* node2 = (int*)my_alloc(16);
//     print_free_list();
//     my_free(node2);
//     print_free_list();
//     my_free(node1);
//     print_free_list();
//     my_clean();

//     return 0;
// }