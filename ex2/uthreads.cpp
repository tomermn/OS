#include "uthreads.h"
#include <set>
#include <unordered_map>
#include <list>
#include <csignal>
#include <sys/time.h>
#include <csetjmp>
#include <cstdlib>
#include <stdio.h>
#include <iostream>

#define JB_SP 6
#define JB_PC 7
#define READY 1
#define RUNNING 2
#define BLOCKED 3
#define USED 1
#define UNUSED 0


// --- thread class Declaration ---

class Thread {
 private:
  int tid;
  int quantums_counter;
  int state;
  char stack[STACK_SIZE];
  thread_entry_point entryPoint;

 public:
  Thread(int tid, thread_entry_point entryPoint);

  ~Thread();

  Thread(const Thread &other); // copy constructor
  Thread &operator=(const Thread &other); // copy assignment
  int get_tid() const;

  int get_state() const;

  int get_thread_quantums() const;

  void increment_quantums();

  void set_state(int);

  char get_stack() const;
};



// --- Data structures and general functions ---

std::unordered_map<int, int> ids_map; // key is tid, val is status (used, unused)
std::unordered_map<int, Thread *> active_threads; // key is tid, val is ptr to thread
std::set<int> blocked_threads; // set of all tid of blocked threads
std::unordered_map<int, int> sleeping_threads; // key is tid, val is num of quantums if sleepeing.
std::list<int> ready_q; // tid
int running_thread;

void timed_switch(int);

void close_program();

sigjmp_buf env[MAX_THREAD_NUM + 1];
int total_quantum_num;
typedef unsigned long address_t;


void block_timer_signal() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  sigprocmask(SIG_BLOCK, &set, nullptr);
}

void unblock_timer_signal() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &set, nullptr);
}


void awake_thread(int tid) {
  if ((active_threads.find(tid) == active_threads.end()) or (sleeping_threads.find(tid) == sleeping_threads.end())) {
      return;
    }
  if (blocked_threads.find(tid) == blocked_threads.end()) {
      active_threads[tid]->set_state(READY);
      ready_q.push_back(tid);
    }
  sleeping_threads.erase(tid);
}


// --- thread class implementation ---

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr) {
  address_t ret;
  asm volatile("xor    %%fs:0x30,%0\n"
               "rol    $0x11,%0\n"
  : "=g" (ret)
  : "0" (addr));
  return ret;
}

Thread::Thread(int tid, thread_entry_point entryPoint) {
  this->tid = tid;
  this->state = READY;
  this->entryPoint = entryPoint;
  this->quantums_counter = 0;
  sigsetjmp(env[tid], 1);
  address_t sp = (address_t) this->stack + STACK_SIZE - sizeof(address_t);
  address_t pc = (address_t) entryPoint;
  (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
  (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
  sigemptyset(&env[tid]->__saved_mask);
}

Thread::~Thread() {
}

Thread::Thread(const Thread &other) {
  this->tid = other.tid;
  this->state = other.state;
  this->quantums_counter = other.quantums_counter;
  for (int i = 0; i < STACK_SIZE; i++) {
      this->stack[i] = other.stack[i];
    }
  this->entryPoint = other.entryPoint;
}

Thread &Thread::operator=(const Thread &other) {
  if (this != &other) {
      this->tid = other.tid;
      this->state = other.state;
      this->quantums_counter = other.quantums_counter;
      for (int i = 0; i < STACK_SIZE; i++) {
          this->stack[i] = other.stack[i];
        }
      this->entryPoint = other.entryPoint;
    }
  return *this;
}

/*getters and setters of Thread*/

int Thread::get_thread_quantums() const {
  return this->quantums_counter;
}

void Thread::increment_quantums() {
  this->quantums_counter++;
}

int Thread::get_tid() const {
  return this->tid;
}

int Thread::get_state() const {
  return this->state;
}

void Thread::set_state(int new_state) {
  this->state = new_state;
}




// --- scheduler implementation ---
class Scheduler {
 private:
  int quantum_usecs;
  struct sigaction sa = {0};
  struct itimerval timer;

 public:
  Scheduler() {}

  Scheduler(int quantum_usecs) {
    this->quantum_usecs = quantum_usecs;
  }

  const itimerval *get_timer() { return &this->timer; }

  void reset_timer() {
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
        std::cerr << "system error: timer error" << std::endl;
        exit(1);
      }
  }

  void set_timer() {
    sa.sa_handler = &timed_switch;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0) {
        std::cerr << "system error: timer error" << std::endl;
        exit(1);
      }
    timer.it_value.tv_sec = this->quantum_usecs / 1000000;        // first time interval, seconds part
    timer.it_value.tv_usec = this->quantum_usecs % 1000000;        // first time interval, microseconds part
    timer.it_interval.tv_sec = this->quantum_usecs / 1000000;     // following time intervals, seconds part
    timer.it_interval.tv_usec = this->quantum_usecs % 1000000;    // following time intervals, microseconds part
    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
        std::cerr << "system error: timer error" << std::endl;
        exit(1);
      }
  }
};


Scheduler scheduler;

void reduce_sleep_quantums() {
  for (auto sleep_t = sleeping_threads.begin(); sleep_t != sleeping_threads.end(); sleep_t++) {
      sleep_t->second--;
      if (sleep_t->second == 0) {
          awake_thread(sleep_t->first);
        }
    }
}

void prepare_next_running() {
  running_thread = ready_q.front();
  ready_q.pop_front();
  active_threads[running_thread]->set_state(RUNNING);
  active_threads[running_thread]->increment_quantums();
  siglongjmp(env[running_thread], 1);
}

void reset_timer() {
  if (setitimer(ITIMER_VIRTUAL, scheduler.get_timer(), nullptr)) {
      std::cerr << "system error: timer error" << std::endl;
      exit(1);
    }
}

void forced_switch(int to_sleep = -1, bool to_block = false, bool to_terminate = false) {
  block_timer_signal();
  reduce_sleep_quantums();
  total_quantum_num++;
  int prev_thread = running_thread;
  int ret_val = sigsetjmp(env[running_thread], 1);
  if (ret_val == 0) {
      if (to_block) {
          active_threads[prev_thread]->set_state(BLOCKED);
          blocked_threads.insert(prev_thread);
        } else if (to_sleep >= 0) {
          active_threads[prev_thread]->set_state(BLOCKED);
          sleeping_threads[prev_thread] = to_sleep;

        } else if (to_terminate) {
          delete active_threads[prev_thread];
          active_threads.erase(prev_thread);
          ids_map[prev_thread] = UNUSED;
        } else {
          active_threads[prev_thread]->set_state(READY);
          ready_q.push_back(prev_thread);
        }
      prepare_next_running();
    }
  reset_timer();
  unblock_timer_signal();
}


void timed_switch(int sig) {
  block_timer_signal();
  total_quantum_num++;
  reduce_sleep_quantums();
  int prev_thread = running_thread;
  int ret_val = sigsetjmp(env[running_thread], 1);
  if (ret_val == 0) {
      active_threads[prev_thread]->set_state(READY);
      ready_q.push_back(prev_thread);
      prepare_next_running();
    }
  reset_timer();
  unblock_timer_signal();
}


// --- uthread library implementation ---

int uthread_init(int quantum_usecs) {

  if (quantum_usecs <= 0) {
      std::cerr << "thread library error: invalid quantum_usecs" << std::endl;
      return -1;
    }
  //initialize ids_map
  for (int i = 1; i < MAX_THREAD_NUM; i++) {
      ids_map[i] = UNUSED;
    }
  scheduler = *new Scheduler(quantum_usecs);
  scheduler.set_timer();
  active_threads[0] = new Thread(0, nullptr);
  if (!active_threads[0]) {
      std::cerr << "system error: Memory allocation fails" << std::endl;
      exit(1);
    }
  active_threads[0]->set_state(RUNNING);
  active_threads[0]->increment_quantums();
  running_thread = 0;
  total_quantum_num = 1;
  return EXIT_SUCCESS;
}

int uthread_spawn(thread_entry_point entry_point) {
  block_timer_signal();
  if (entry_point == nullptr){
      std::cerr << "thread library error: thread cannot get nullptr as entry_point" << std::endl;
      unblock_timer_signal();
      return -1;
    }
  int tid = -1;
  for (int i = 1; i < MAX_THREAD_NUM; i++) {
      if (ids_map[i] == UNUSED) {
          tid = i;
          ids_map[i] = USED;
          break;
        }
    }
  if (tid == -1) {
      std::cerr << "thread library error: you reached the max number of threads" << std::endl;
      unblock_timer_signal();
      return -1;
    }


  Thread *newThread = new(std::nothrow) Thread(tid, entry_point);
  if (!newThread) {
      std::cerr << "system error: Memory allocation fails" << std::endl;
      exit(1);
    }
  active_threads[tid] = newThread;
  ready_q.push_back(tid);
  unblock_timer_signal();
  return tid;
}

int uthread_terminate(int tid) {
  block_timer_signal();
  if (active_threads.find(tid) == active_threads.end()) {
      unblock_timer_signal();
      std::cerr << "thread library error: tid is not exist" << std::endl;
      return -1;
    }

  if (tid == 0) {
      close_program();
    }
  if (active_threads[tid]->get_state() == RUNNING) {
      forced_switch(-1, false, true);
      return EXIT_SUCCESS;
    }
  if (active_threads[tid]->get_state() == READY) {
      ready_q.remove(tid);
    }

  if (active_threads[tid]->get_state() == BLOCKED) {
      if (sleeping_threads.find(tid) != sleeping_threads.end()) { sleeping_threads.erase(tid); }
      if (blocked_threads.find(tid) != blocked_threads.end()) { blocked_threads.erase(tid); }
    }
  delete active_threads[tid];
  active_threads.erase(tid);
  ids_map[tid] = UNUSED;
  unblock_timer_signal();
  return EXIT_SUCCESS;
}

void close_program() {
  for (auto it = active_threads.begin(); it != active_threads.end(); it++) {
      delete it->second;
    }
  exit(0);
}

int uthread_block(int tid) {
  block_timer_signal();
  if (active_threads.find(tid) == active_threads.end()) {
      std::cerr << "thread library error: tid is not exist" << std::endl;
      unblock_timer_signal();
      return -1;
    }
  if (tid == 0) {
      std::cerr << "thread library error: main thread cannot be blocked" << std::endl;
      unblock_timer_signal();
      return -1;
    }

  if (active_threads[tid]->get_state() == READY) {
      ready_q.remove(tid);
    }

  if (active_threads[tid]->get_state() == RUNNING) {
      forced_switch(-1, true, false);
      return EXIT_SUCCESS;
    }
  active_threads[tid]->set_state(BLOCKED);
  blocked_threads.insert(tid);
  unblock_timer_signal();
  return EXIT_SUCCESS;
}


int uthread_resume(int tid) {
  block_timer_signal();
  auto curr_thread = active_threads.find(tid);
  if (curr_thread == active_threads.end()) { //tid is not exist
      std::cerr << "thread library error: tid is not exist" << std::endl;
      unblock_timer_signal();
      return -1;
    }
  auto curr_state = curr_thread->second->get_state();
  bool is_sleeping = sleeping_threads.find(tid) != sleeping_threads.end();

  if ((curr_state == BLOCKED) and is_sleeping) 
    {
      blocked_threads.erase(curr_thread->first);
    } else if (curr_state == BLOCKED) {
      curr_thread->second->set_state(READY);
      blocked_threads.erase(tid);
      ready_q.push_back(tid);
    }
  unblock_timer_signal();
  return EXIT_SUCCESS;
}


int uthread_sleep(int num_quantums) {
  block_timer_signal();
  if (running_thread == 0) {
      std::cerr << "thread library error: main thread can't call uthread_sleep function" << std::endl;
      unblock_timer_signal();
      return -1;
    }
  if (num_quantums <= 0) {
      std::cerr << "thread library error: num_quantums must be positive" << std::endl; //TODO - is it?
      unblock_timer_signal();
      return -1;
    }
  forced_switch(num_quantums);
  unblock_timer_signal();
  return EXIT_SUCCESS;
}


int uthread_get_tid() {
  return running_thread;
}

int uthread_get_total_quantums() {
  return total_quantum_num;
}


int uthread_get_quantums(int tid) {
  block_timer_signal();
  if (active_threads.find(tid) == active_threads.end()) {
      std::cerr << "thread library error: tid is not exist" << std::endl;
      unblock_timer_signal();
      return -1;
    }
  int quantums = active_threads[tid]->get_thread_quantums();
  unblock_timer_signal();
  return quantums;
}



