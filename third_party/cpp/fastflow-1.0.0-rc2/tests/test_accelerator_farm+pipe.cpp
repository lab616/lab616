/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ***************************************************************************
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  As a special exception, you may use this file as part of a free software
 *  library without restriction.  Specifically, if other files instantiate
 *  templates or use macros or inline functions from this file, or you compile
 *  this file and link it with other files to produce an executable, this
 *  file does not by itself cause the resulting executable to be covered by
 *  the GNU General Public License.  This exception does not however
 *  invalidate any other reasons why the executable file might be covered by
 *  the GNU General Public License.
 *
 ****************************************************************************
 */

/*
 * Mixing FastFlow farm with pipeline workers in the accelerator mode
 *
 *
 *                        |(stage1->stage2)|   
 *                        |                | 
 *    main-flow ---->farm |(stage1->stage2)|---
 *        .               |                |   |
 *        .               |(stage1->stage2)|   |
 *        .                                    |
 *    main-flow <------------------------------         
 *
 *
 */

#include <stdlib.h>
#include <iostream>
#include <pipeline.hpp>
#include <farm.hpp>
#include <allocator.hpp>

#include <sys/time.h>
#include <sys/resource.h>
#ifdef __linux__
#include <asm/unistd.h>
#define gettid() syscall(__NR_gettid)
#else
#define gettid() 0
#endif

using namespace ff;

enum { IN_QUEUE_SIZE=1024, OUT_QUEUE_SIZE=2048};

// define your task
typedef int task_t;


/*
 * priority_level is a value in the range -20 to 19.
 * The default priority is 0, lower priorities cause more favorable scheduling.
 *
 */
static inline int setPriority(int priority_level=0) {
    if (priority_level) {
        if (setpriority(PRIO_PROCESS, gettid(),priority_level) != 0) {
            perror("setpriority:");
            return -2;
        }
    }
    return 0;
}


class Stage1: public ff_node {
public:
    Stage1(int priority_level):priority_level(priority_level) {}

    int svc_init() {
        return setPriority(priority_level);
    }

    void * svc(void * task) {
        std::cout << "Stage1 got task\n";
        return task;
    }
private:
    int priority_level;
};

class Stage2: public ff_node {
public:
    Stage2(int priority_level):priority_level(priority_level) {}

    int svc_init() {
        return setPriority(priority_level);
    }

    void * svc(void * task) {
        std::cout << "Stage2 got task\n";
        return task;
    }
private:
    int priority_level;
}; 


class Emitter: public ff_node {
public:
    Emitter(int priority_level):priority_level(priority_level) {}

    int svc_init() {
        return setPriority(priority_level);
    }

    void * svc(void * task) { return task;}

private:
    int priority_level;   
};

class Collector: public ff_node {
public:
    Collector(int priority_level):priority_level(priority_level) {}

    int svc_init() {
        return setPriority(priority_level);
    }

    void * svc(void * task) { return task;}

private:
    int priority_level;
};



int main(int argc, char * argv[]) {
    task_t * result = NULL;

    int mstreamlen= 0;
    int nworkers  = 0;
    int iterations= 0;
    int priority  = 0;

    if (argc != 4 && argc != 5) {
        std::cerr << "use:\n" << " " << argv[0] << " max-stream-length num-farm-workers iterations [priority]\n";
        std::cerr << " NOTE: <priority> values less then 0 require CAP_SYS_NICE capability\n\n";
        return -1;
    }
    mstreamlen=atoi(argv[1]);
    nworkers  =atoi(argv[2]);
    iterations=atoi(argv[3]);

    if (argc == 5) {
        priority=atoi(argv[4]);
    }

    srandom(::getpid()+(getusec()%4999)); // init seed
     
    // build the farm module
    ff_farm<> farm(true, IN_QUEUE_SIZE, OUT_QUEUE_SIZE);
    farm.set_scheduling_ondemand(); // set on-demand scheduling policy

    // we just want a generic gather without any user filter
    //farm.add_collector(NULL); 

    Emitter E(priority);
    Collector C(priority);
    farm.add_collector(&E);
    farm.add_emitter(&C); 

    std::vector<ff_node *> w;
    for(int i=0;i<nworkers;++i) {
        // build worker pipeline 
        ff_pipeline * pipe = new ff_pipeline;
        pipe->add_stage(new Stage1(priority));
        pipe->add_stage(new Stage2(priority));
        w.push_back(pipe);
    }
    farm.add_workers(w);

    /*--- we start and immediatly freeze all the threads -- */
    farm.offload((void *)FF_EOS);
    if (farm.run_then_freeze()<0) {
        error("running farm\n");
        return -1;
    }
    if (farm.wait_freezing()<0) {
        error("waiting farm freezing\n");
        return -1;
    }
    farm.load_result((void **)&result); // pop out the EOS
    /* ---------------------------------------------------- */

    for(int i=0;i<iterations;++i) {
        
        // prepare a buch of tasks to be offloaded
        int bunch = ::random() % mstreamlen;

        if (farm.run_then_freeze()<0) {
            error("running farm\n");
            return -1;
        }
        

        for(int j=0;j<bunch;++j) {
            task_t * task = new task_t(i+j);

            if (farm.offload(task)<0) {
                error("offloading task\n");
                return -1;
            }

            // Try to get results, if there are any
            // If there aren't any deadlock problems, the following piece 
            // of code can be moved outside the for-j loop using 
            // the synchronous load_result method.
            if (farm.load_result_nb((void **)&result)) {
                std::cout << "result= " << *((int*)result) << "\n";

                /* do something useful, probably using another thread */

                delete ((int*)result);
            }            
        }

        // offload End-Of-Stream
        if (farm.offload((void *)FF_EOS)<0) {
            error("offloading EOS\n");
            return -1;
        }

        // asynchronously wait results
        do {
            if (farm.load_result_nb((void **)&result)) {
                if (result==(void*)FF_EOS) break;

                /* do something useful, probably using onother thread */

                std::cout << "result= " << *((int*)result) << "\n";
                delete ((int*)result);
            } 

            /* do something else */
            
        } while(1);

        // here join
        if (farm.wait_freezing()<0) {
            error("waiting farm freezing\n");
            return -1;
        }
    }

    // wait all threads join
    if (farm.wait()<0) {
        error("waiting farm freezing\n");
        return -1;
    }

    std::cerr << "DONE, time= " << farm.ffTime() << " (ms)\n";
    farm.ffStats(std::cout);

    return 0;
}
