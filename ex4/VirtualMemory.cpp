#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <cstdlib>


#define ARR_SIZE 12
#define MAX_FRAME 0
#define CURR_DEPTH 1
#define FRAME_TO_REPLACE 2
#define IS_ZERO 3
#define CURR_FRAME 4
#define CURR_PAGE 5
#define CYCLIC_DISTANCE 6
#define PARENT_OF_FRAME_TO_REPLACE 7
#define CURR_PARENT 8
#define PAGE_SWAPPED_IN 9
#define PAGE_TO_REPLACE 10
#define CALLING_FRAME 11

uint64_t get_offset(uint64_t addr, int i);
int get_page_num(uint64_t addr);
int* traverse_on_tree(int* data_arr);

// --find an unused frame or evict a page from some frame.


void print_frame_0(int va);

int* calculate_cyclic_distance(int* data_arr){
  int b = abs(data_arr[PAGE_SWAPPED_IN] - data_arr[CURR_PAGE]);
  int a = NUM_PAGES - b;
  int cyc_dis = a < b ? a : b;
  if (cyc_dis > data_arr[CYCLIC_DISTANCE]){
      data_arr[CYCLIC_DISTANCE] = cyc_dis;
      data_arr[FRAME_TO_REPLACE] = data_arr[CURR_FRAME];
      data_arr[PARENT_OF_FRAME_TO_REPLACE] = data_arr[CURR_PARENT];
      data_arr[PAGE_TO_REPLACE] = data_arr[CURR_PAGE];
    }
  return data_arr;
}


int* dfs_on_tree(int* data_arr){
  int next_frame;
  int curr_depth = data_arr[CURR_DEPTH], curr_frame = data_arr[CURR_FRAME];
  int num_of_sons = PAGE_SIZE;
  if (curr_depth == 0 && (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH != 0)){
      num_of_sons = 2 ^ (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH);
  }
  for(int i = 0 ; i < num_of_sons ; i++){
      PMread((curr_frame * PAGE_SIZE) + i, &next_frame);
      if (next_frame == 0){
          continue;
        }
      data_arr[CURR_PARENT] = curr_frame;
      data_arr[CURR_FRAME] = next_frame;
      data_arr[CURR_PAGE] = data_arr[CURR_PAGE] << (OFFSET_WIDTH);
      data_arr[CURR_PAGE] += i;
//      printf("%lld\n", data_arr[CURR_PAGE] );
//      fflush(stdout);
      data_arr[CURR_DEPTH] = curr_depth + 1;

      traverse_on_tree(data_arr);
      data_arr[CURR_PAGE] -= i;
      data_arr[CURR_PAGE] = data_arr[CURR_PAGE] >> (OFFSET_WIDTH);

      if (data_arr[IS_ZERO]){
          return data_arr;
        }
    }
  return data_arr;
}


bool is_frame_empty(const int* data_arr){
  int next_frame;
  for(int i = 0 ; i < PAGE_SIZE ; i++){
      PMread((data_arr[CURR_FRAME] * PAGE_SIZE) + i, &next_frame);
      if (next_frame != 0){
          return false;
        }
    }
  return true;
}

int* traverse_on_tree(int* data_arr){
//  if (data_arr[IS_ZERO]){
//      return data_arr;
//    }
  if (data_arr[MAX_FRAME] < data_arr[CURR_FRAME]){
      data_arr[MAX_FRAME] = data_arr[CURR_FRAME];
    }
  // --- check if in page - cyclic distance ---
  if (data_arr[CURR_DEPTH] == TABLES_DEPTH){
      return calculate_cyclic_distance(data_arr);
    }

  // --- check if frame is empty - all rows are 0 ---
  if (data_arr[CURR_FRAME] != data_arr[CALLING_FRAME] && is_frame_empty(data_arr)){
      data_arr[FRAME_TO_REPLACE] = data_arr[CURR_FRAME];
      data_arr[PARENT_OF_FRAME_TO_REPLACE] = data_arr[CURR_PARENT];
      data_arr[IS_ZERO] = 1;
      return data_arr;
    }

  // --- call in dfs way on children and return the calculated arr ---
  return dfs_on_tree(data_arr);
}


void update_parent(int* data_arr){
  int val, addr;
  for(int i = 0 ; i < PAGE_SIZE ; i++){
      addr = (int) (data_arr[PARENT_OF_FRAME_TO_REPLACE] * PAGE_SIZE) + i;
      PMread(addr, &val);
      if (val == data_arr[FRAME_TO_REPLACE]){
          PMwrite(addr, 0);
        }
    }
}


int find_frame(int page_num, int calling_frame) {
  int data_arr[ARR_SIZE] = {0, 0, 0, 0, 0, 0, 0,
                            0, 0, page_num, 0, calling_frame};

  traverse_on_tree(data_arr);

  if (data_arr[IS_ZERO]){ // found a frame with only zeros
      update_parent(data_arr);
      return data_arr[FRAME_TO_REPLACE];
    }

  if (data_arr[MAX_FRAME] < NUM_FRAMES - 1){ // max frame is not max
      data_arr[FRAME_TO_REPLACE] = data_arr[MAX_FRAME] + 1;
      return data_arr[FRAME_TO_REPLACE];
    }

  // cyclic distance

  PMevict(data_arr[FRAME_TO_REPLACE], data_arr[PAGE_TO_REPLACE]);
  update_parent(data_arr);
  return data_arr[FRAME_TO_REPLACE];
}


// --write 0 in all the frame content (only necessary if next layer is a table)
void create_new_table(uint64_t frame_addr) {
  for (int i = 0; i < PAGE_SIZE; i++) {
      PMwrite(frame_addr * PAGE_SIZE + i, 0);
    }
}

void VMinitialize() {
  for (int i = 0; i < PAGE_SIZE; i++) {
      PMwrite(i, 0);
    }
}


uint64_t get_physical_address(uint64_t virtualAddress){
    int table_i;
    if (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH == 0){
        table_i = VIRTUAL_ADDRESS_WIDTH / OFFSET_WIDTH;
    }
    else{
        table_i = (VIRTUAL_ADDRESS_WIDTH / OFFSET_WIDTH) + 1;
    }
  int curr_frame = 0, next_frame, page_num;
  uint64_t curr_offset;
  for (int i = 0; i < TABLES_DEPTH; i++) {
      curr_offset = get_offset(virtualAddress, table_i--);
      PMread((curr_frame * PAGE_SIZE) + curr_offset, &next_frame);
      if (next_frame == 0) { // create table or evict
          page_num = get_page_num(virtualAddress);
          next_frame = find_frame(page_num, curr_frame);

          PMwrite((curr_frame * PAGE_SIZE) + curr_offset, next_frame); // write new frame to old
          if (i == TABLES_DEPTH - 1){
              PMrestore(next_frame, page_num);
            }
          else{
              create_new_table(next_frame);
            }
        }
      curr_frame = next_frame;
    }
  curr_offset = get_offset(virtualAddress, table_i--);
  return (curr_frame * PAGE_SIZE) + curr_offset;
}


int VMread(uint64_t virtualAddress, word_t *value) {
    if(virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return 0;
    }
    uint64_t phy_addr = get_physical_address(virtualAddress);
  PMread(phy_addr, value);
  return 1;
}


int VMwrite(uint64_t virtualAddress, word_t value) {
    if(virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return 0;
    }
  uint64_t phy_addr = get_physical_address(virtualAddress);
  PMwrite(phy_addr, value);
  return 1;
}


uint64_t get_offset(uint64_t addr, int i) {
  uint64_t a = (addr >> (OFFSET_WIDTH * (i - 1)));
  uint64_t b =  a & ((1 << OFFSET_WIDTH) - 1);
  return b;
}


int get_page_num(uint64_t addr){
  return (int) (addr >> (OFFSET_WIDTH));
}

