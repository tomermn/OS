#include <memory>
#include <pthread.h>
#include <algorithm>
#include <unordered_map>
#include <atomic>
#include <semaphore.h>
#include <iostream>
#include "MapReduceFramework.h"
#include "Barrier.h"

class JobContext;

struct
ThreadContext {
    int threadID;
    JobContext* job_context;
    IntermediateVec* mid_vec;
};

class JobContext {
public:
    //counters:
    std::atomic<int>* map_counter;
    std::atomic<int>* num_mid_vec_elements;
    std::atomic<int>* shuffle_counter;
    std::atomic<int>* output_counter;
    std::atomic<uint64_t>* state_atomic;

    //data structures:
    pthread_t* threads;
    std::unordered_map<int, ThreadContext*>* threads_contexts_map;
    InputVec input_vec;
    OutputVec* output_vec;
    std::vector<IntermediateVec>* shuffle_queue;
    const MapReduceClient* client;
    Barrier* barrier = nullptr;
    pthread_mutex_t reduce_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;


    //info:
    int input_vec_size;
    int total_num_of_threads;
    bool threads_terminated = false;


    JobContext(const InputVec& input_vec, OutputVec* output_vec, size_t vec_size,
               const MapReduceClient* client, int total_threads, pthread_t* threads):\

               map_counter(new std::atomic<int>(0)),
               num_mid_vec_elements(new std::atomic<int>(0)),
               shuffle_counter(new std::atomic<int>(0)),
               output_counter(new std::atomic<int>(0)),
               state_atomic(new std::atomic<uint64_t>(0))

                       {
        this->threads = threads;
        this->total_num_of_threads = total_threads;
        this->input_vec = input_vec;
        this->output_vec = output_vec;
        this->client = client;
        this->input_vec_size = (int) vec_size;
        this->barrier = new Barrier(total_threads);
        this->shuffle_queue = new std::vector<IntermediateVec>;
        this->threads_contexts_map = nullptr;
    }

    void kill_counters() const{
        delete this->map_counter;
        delete this->num_mid_vec_elements;
        delete this->shuffle_counter;
        delete this->output_counter;
        delete this->state_atomic;
    }

    void free_context_map() const {
        for (auto context : *this->threads_contexts_map) {
            if (context.second) {
                delete context.second->mid_vec;
                delete context.second;
            }
        }
        delete this->threads_contexts_map;
    }

    ~JobContext(){
        this->kill_counters();
        this->free_context_map();
        delete this->barrier;
        delete this->shuffle_queue;
        delete [] this->threads;
    }
};



void* thread_job(void* arg);



// -- library implementation --

JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec,
                            int multiThreadLevel){
    auto* threads = new pthread_t[multiThreadLevel];
    auto* threads_contexts_map = new std::unordered_map<int, ThreadContext*>();
    auto* job = new JobContext(inputVec, &outputVec, inputVec.size(),
                               &client, multiThreadLevel, threads);

    for (int i = 0; i < multiThreadLevel; ++i) {
        (*threads_contexts_map)[i] = new ThreadContext{i, job,
                                                       new IntermediateVec ()};
    }
    job->threads_contexts_map = threads_contexts_map;

    for (int i = 0; i < multiThreadLevel; ++i) {
        if (pthread_create(threads + i, nullptr, thread_job,
                           (*threads_contexts_map)[i]) != 0){
            std::cout << "system error: pthread_create did not work" << std::endl;
            exit(1);
        }
    }
    return job;
}

bool compare(const IntermediatePair& p1, const IntermediatePair& p2){
    return *p1.first < *p2.first;
}


K2* get_max_element(ThreadContext* tc){
    K2* max_key = nullptr;
    for (int i = 0 ; i < tc->job_context->total_num_of_threads ; i++){
        ThreadContext* cur = tc->job_context->threads_contexts_map->at(i);
        if(max_key != nullptr && !cur->mid_vec->empty()){
            K2* is_max = cur->mid_vec->back().first;
            if (*max_key < *is_max) {
                max_key = is_max;
            }
        }
        else if(!cur->mid_vec->empty()){
            max_key = cur->mid_vec->back().first;
        }
    }
    return max_key;
}

void fill_sequence(IntermediateVec* sequence, ThreadContext* tc, K2* max_key){
    for (int i = 0 ; i < tc->job_context->total_num_of_threads ; i++) {
        auto* cur_mid_vec = tc->job_context->threads_contexts_map->at(i)->mid_vec;
        while (!cur_mid_vec->empty() && !(*cur_mid_vec->back().first < *max_key) &&
        !(*max_key < *cur_mid_vec->back().first)) {
            auto pair_to_push = cur_mid_vec->back();
            sequence->push_back(pair_to_push);
            cur_mid_vec->pop_back();
            (*(tc->job_context->state_atomic)) += 1; // update finished shuffle phase
        }
    }
}

bool intermed_not_empty(ThreadContext* tc){
    for (int i = 0; i < (int) tc->job_context->threads_contexts_map->size() ; i++){
        if (!tc->job_context->threads_contexts_map->at(i)->mid_vec->empty()){
            return true;
        }
    }
    return false;
}

void thread_map_phase(ThreadContext* tc){
    if(pthread_mutex_lock(&tc->job_context->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_lock did not work" << std::endl;
        exit(1);
    }

    if (*tc->job_context->state_atomic >> 62 == 0){
        (*(tc->job_context->state_atomic)) = ((uint64_t)tc->job_context->
                input_vec_size << 31) + ((uint64_t)1 << 62);
    }
    if(pthread_mutex_unlock(&tc->job_context->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_unlock did not work" << std::endl;
        exit(1);
    }

    int self_job_counter = 0;
    while (*(tc->job_context->map_counter) < tc->job_context->input_vec_size){
        int old_value = (*(tc->job_context->map_counter))++;
        if(old_value < tc->job_context->input_vec_size){
            auto curr_pair = tc->job_context->input_vec.at(old_value);
            tc->job_context->client->map(curr_pair.first, curr_pair.second, tc);
            self_job_counter++;
        }
    }

    //--sort phase--
    std::sort(tc->mid_vec->begin(), tc->mid_vec->end(), compare);

    (*(tc->job_context->state_atomic)) += self_job_counter; // update finished map-sort phase
}

void thread_shuffle_phase(ThreadContext* tc){
    while (intermed_not_empty(tc)) {
        K2 *max_key = get_max_element(tc);
        auto sequence = new IntermediateVec();
        fill_sequence(sequence, tc, max_key);
        tc->job_context->shuffle_queue->push_back(*sequence);
        delete sequence;
        (*tc->job_context->shuffle_counter)++;
    }
}

void thread_reduce_phase(ThreadContext* tc){
    if(pthread_mutex_lock(&tc->job_context->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_lock did not work" << std::endl;
        exit(1);
    }
    if (*tc->job_context->state_atomic >> 62 == 2){
        (*(tc->job_context->state_atomic)) = ((uint64_t)*tc->job_context->
                shuffle_counter << 31) + ((uint64_t)3 << 62);
    }
    if(pthread_mutex_unlock(&tc->job_context->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_unlock did not work" << std::endl;
        exit(1);
    }

    IntermediateVec vec_to_reduce;
    while (!tc->job_context->shuffle_queue->empty()){
        pthread_mutex_lock(&tc->job_context->reduce_mutex);
        if (!tc->job_context->shuffle_queue->empty()){
            vec_to_reduce = tc->job_context->shuffle_queue->back();
            tc->job_context->shuffle_queue->pop_back();
        }
        else{
            if(pthread_mutex_unlock(&tc->job_context->reduce_mutex) != 0){
                std::cout << "system error: pthread_mutex_unlock did not work" << std::endl;
                exit(1);
            }
            break;
        }
        //end mutex
        tc->job_context->client->reduce(&vec_to_reduce, tc);
        if(pthread_mutex_unlock(&tc->job_context->reduce_mutex) != 0){
            std::cout << "system error: pthread_mutex_unlock did not work" << std::endl;
            exit(1);
        }
        (*(tc->job_context->state_atomic)) += 1; // update finished reduce phase
    }
}


void* thread_job(void* arg)
{
    auto* tc = (ThreadContext*) arg;

    //--map phase--
    thread_map_phase(tc);

    //barrier before shuffle
    tc->job_context->barrier->barrier();

//     --shuffle phase--
    if(pthread_mutex_lock(&tc->job_context->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_lock did not work" << std::endl;
        exit(1);
    }
    if (*tc->job_context->state_atomic >> 62 == 1){
        (*(tc->job_context->state_atomic)) = ((uint64_t)*tc->job_context->
                num_mid_vec_elements << 31) + ((uint64_t)2 << 62);
    }
    if(pthread_mutex_unlock(&tc->job_context->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_unlock did not work" << std::endl;
        exit(1);
    }

    if (tc->threadID == 0){
        thread_shuffle_phase(tc);
    }

    //barrier after shuffle
    tc->job_context->barrier->barrier();

    // --reduce phase--
    thread_reduce_phase(tc);

    pthread_exit(nullptr);
}



void emit2 (K2* key, V2* value, void* context){
    auto* tc = (ThreadContext*) context;
    IntermediatePair new_pair = std::make_pair(key, value);
    tc->mid_vec->push_back(new_pair);
    (*tc->job_context->num_mid_vec_elements)++;
}


void emit3 (K3* key, V3* value, void* context){
    auto* tc = (ThreadContext*) context;
    OutputPair new_pair = std::make_pair(key, value);
    tc->job_context->output_vec->push_back(new_pair);
    (*tc->job_context->output_counter)++;
}


void closeJobHandle(JobHandle job){
    auto* jc = (JobContext*) job;
    waitForJob(job);
    delete jc;
}

void getJobState(JobHandle job, JobState* state){
    auto* jc = (JobContext*) job;
    if(pthread_mutex_lock(&jc->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_lock did not work" << std::endl;
        exit(1);
    }
    auto* state_counter = jc->state_atomic;
    auto divider = (float) ((*state_counter >> 31) & 0x7fffffff);
    if (divider == 0){
        state->percentage = 0;
    }
    else{
    state->percentage = (float) (*state_counter & 0x7fffffff) / divider * 100;
    }
    state->stage = stage_t(*state_counter >> 62);
    if(pthread_mutex_unlock(&jc->state_mutex) != 0){
        std::cout << "system error: pthread_mutex_unlock did not work" << std::endl;
        exit(1);
    }

}

void waitForJob(JobHandle job){
    auto jc = (JobContext*) job;
    if (jc->threads_terminated){
        return;
    }
    jc->threads_terminated = true;
    for(int i = 0 ; i < jc->total_num_of_threads ; i++){
        if(pthread_join(jc->threads[i], nullptr) != 0){
            std::cout << "system error: pthread_join did not work" << std::endl;
            exit(1);
        }
    }
}