#include <iostream>
#include <sys/time.h>
#include "osm.h"


unsigned int round_up(unsigned int iterations)
{
  while (iterations % 10 != 0)
    {
      iterations ++;
    }
  return iterations;
}


double osm_operation_time(unsigned int iterations){
  if(iterations < 1){
      return -1;
    }
  int res = 0;
  timeval tv;
  if(gettimeofday(&tv, nullptr) != 0){
        return -1;
  }
  double mic_start = tv.tv_usec;
  double sec_start = tv.tv_sec;
  iterations = round_up(iterations);
  for (unsigned int i = 0; i < iterations; i+=10) {
      res = res + 1;
      res = res + 1;
      res = res + 1;
      res = res + 1;
      res = res + 1;
      res = res + 1;
      res = res + 1;
      res = res + 1;
      res = res + 1;
      res = res + 1;
    }
  if(gettimeofday(&tv, nullptr) != 0){
        return -1;
  }
  double mic_end = tv.tv_usec;
  double sec_end = tv.tv_sec;
  double final = (sec_end - sec_start) * 1000000000 + (mic_end - mic_start) * 1000;
  return final / iterations;
}

void empty_func() {}

double osm_function_time(unsigned int iterations){
  if (iterations < 1) {return -1;}
  timeval tv;
  if(gettimeofday(&tv, nullptr) != 0){
        return -1;
  }
  double mic_start = tv.tv_usec;
  double sec_start = tv.tv_sec;
  iterations = round_up (iterations);
  for (unsigned int i = 0; i < iterations; i += 10){
      empty_func();
      empty_func();
      empty_func();
      empty_func();
      empty_func();
      empty_func();
      empty_func();
      empty_func();
      empty_func();
      empty_func();
  }
  if(gettimeofday(&tv, nullptr) != 0){
        return -1;
  }
  double mic_end = tv.tv_usec;
  double sec_end = tv.tv_sec;
  double final = (sec_end - sec_start) * 1000000000 + (mic_end - mic_start) * 1000;
  return final / iterations;
}


double osm_syscall_time(unsigned int iterations){
  if (iterations < 1) {return -1;}
  timeval tv;
  if(gettimeofday(&tv, nullptr) != 0){
      return -1;
  }
  double mic_start = tv.tv_usec;
  double sec_start = tv.tv_sec;
  iterations = round_up (iterations);
  for (unsigned int i = 0; i < iterations; i += 10){
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
      OSM_NULLSYSCALL;
    }
  if(gettimeofday(&tv, nullptr) != 0){
        return -1;
  }
  double mic_end = tv.tv_usec;
  double sec_end = tv.tv_sec;
  double final = (sec_end - sec_start) * 1000000000 + (mic_end - mic_start) * 1000;
  return final / iterations;
}


//int main ()
//{
//  std::cout<<osm_operation_time (456789)<<std::endl;
//  std::cout<<osm_function_time (456789)<<std::endl;
//  std::cout<<osm_syscall_time (456789)<<std::endl;
//  return 0;
//}
