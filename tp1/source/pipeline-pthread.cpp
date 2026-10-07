#include <pthread.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <thread>
#include <vector>

#include "filter.h"
#include "log.h"
#include "pipeline.h"
#include "queue.h"

constexpr unsigned int QUEUE_SIZE = 4;
constexpr unsigned int N_STAGES   = 5;  // loader + scale_up + desaturate + edge_detect + saver
constexpr unsigned int N_QUEUES   = N_STAGES - 1;

using filter_fn = image_t* (*)(image_t*);
using thread_fn = void* (*)(void*);

struct stage_args {
    thread_fn main_fn;
    image_dir_t* image_dir;
    queue_t* in;
    queue_t* out;
    filter_fn filter;
    unsigned int n_workers;  // threads running this stage
};

static void* load(void* arg) {
    auto* stage = static_cast<stage_args*>(arg);

    while (true) {
        image_t* image = image_dir_load_next(stage->image_dir);
        if (image == nullptr) {
            break;
        }
        queue_push(stage->out, image);
    }

    return nullptr;
}

static void* filter(void* arg) {
    auto* stage = static_cast<stage_args*>(arg);

    while (true) {
        auto* image = static_cast<image_t*>(queue_pop(stage->in));
        if (image == nullptr) {
            break;
        }

        image_t* new_image = stage->filter(image);
        queue_push(stage->out, new_image);

        image_destroy(image);
    }

    return nullptr;
}

static void* save(void* arg) {
    auto* stage = static_cast<stage_args*>(arg);

    while (true) {
        auto* image = static_cast<image_t*>(queue_pop(stage->in));
        if (image == nullptr) {
            break;
        }

        image_dir_save(stage->image_dir, image);
        image_destroy(image);
    }

    return nullptr;
}

int pipeline_pthread(image_dir_t* image_dir) {
    const unsigned int cores = std::max(1u, std::thread::hardware_concurrency());

    std::array<queue_t*, N_QUEUES> queues;

    for (queue_t*& queue : queues) {
        queue = queue_create(QUEUE_SIZE);
    }

    filter_fn filter_scale_up_3 = [](image_t* image) { return filter_scale_up(image, 3); };

    // share of the cores given to a stage (at least one worker)
    auto workers = [cores](double weight) { return std::max(1u, static_cast<unsigned int>(cores * weight)); };

    // weights follow each stage's cost (save ~80 %, edge_detect ~15 %, the rest ~5 %), with slack;
    // the loader must stay alone (image_dir_load_next is not thread-safe)
    std::array<stage_args, N_STAGES> stages{
        {{.main_fn = load, .image_dir = image_dir, .out = queues[0], .n_workers = 1},
         {.main_fn   = filter,
          .in        = queues[0],
          .out       = queues[1],
          .filter    = filter_scale_up_3,
          .n_workers = workers(0.125)},
         {.main_fn   = filter,
          .in        = queues[1],
          .out       = queues[2],
          .filter    = filter_desaturate,
          .n_workers = workers(0.125)},
         {.main_fn   = filter,
          .in        = queues[2],
          .out       = queues[3],
          .filter    = filter_edge_detect,
          .n_workers = workers(0.5)},
         {.main_fn = save, .image_dir = image_dir, .in = queues[3], .n_workers = workers(1.0)}}};

    std::array<std::vector<pthread_t>, N_STAGES> threads;
    bool failed = false;
    for (unsigned int i = 0; i < N_STAGES && !failed; i++) {
        for (unsigned int w = 0; w < stages[i].n_workers; w++) {
            pthread_t thread;
            errno = pthread_create(&thread, nullptr, stages[i].main_fn, &stages[i]);
            if (errno != 0) {
                LOG_ERROR_ERRNO("pthread_create");
                failed = true;
                break;
            }
            threads[i].push_back(thread);
        }
    }

    for (unsigned int i = 0; i < N_STAGES; i++) {
        for (pthread_t thread : threads[i]) {
            pthread_join(thread, nullptr);
        }
        const bool is_last_stage = (i == N_STAGES - 1);
        if (!is_last_stage) {
            // Sending a poison pill to signal the next stage that no more images will come
            for (size_t w = 0; w < threads[i + 1].size(); w++) {
                queue_push(queues[i], nullptr);
            }
        }
    }

    for (queue_t* queue : queues) {
        queue_destroy(queue);
    }

    return failed ? -1 : 0;
}
