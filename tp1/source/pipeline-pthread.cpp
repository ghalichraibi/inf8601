#include <pthread.h>
#include <array>

#include "filter.h"
#include "log.h"
#include "pipeline.h"
#include "queue.h"

constexpr int QUEUE_SIZE   = 4;
constexpr int STAGE_COUNT  = 5;  // loader + scale_up + desaturate + edge_detect + saver
constexpr int THREAD_COUNT = STAGE_COUNT;
constexpr int QUEUE_COUNT  = STAGE_COUNT - 1;

using filter_fn = image_t* (*)(image_t*);
using thread_fn = void* (*)(void*);

struct stage_args {
    thread_fn main_fn;
    image_dir_t* image_dir;
    queue_t* in;
    queue_t* out;
    filter_fn filter;
};

static void* load(void* arg) {
    auto* args     = static_cast<stage_args*>(arg);
    queue_t* queue = args->out;

    while (true) {
        image_t* image = image_dir_load_next(args->image_dir);
        if (image == nullptr) {
            break;
        }
        queue_push(queue, image);
    }

    queue_push(queue, nullptr);  // poison pill to signal the end

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

    queue_push(stage->out, nullptr);  // propagate the poison pill downstream

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
    std::array<pthread_t, THREAD_COUNT> threads;
    std::array<queue_t*, QUEUE_COUNT> queues;

    for (queue_t*& queue : queues) {
        queue = queue_create(QUEUE_SIZE);
    }

    filter_fn filter_scale_up_3 = [](image_t* image) { return filter_scale_up(image, 3); };

    std::array<stage_args, STAGE_COUNT> stages{
        {{.main_fn = load, .image_dir = image_dir, .out = queues[0]},
         {.main_fn = filter, .in = queues[0], .out = queues[1], .filter = filter_scale_up_3},
         {.main_fn = filter, .in = queues[1], .out = queues[2], .filter = filter_desaturate},
         {.main_fn = filter, .in = queues[2], .out = queues[3], .filter = filter_edge_detect},
         {.main_fn = save, .image_dir = image_dir, .in = queues[3]}}};

    for (int i = 0; i < STAGE_COUNT; i++) {
        pthread_create(&threads[i], nullptr, stages[i].main_fn, &stages[i]);
    }

    for (pthread_t thread : threads) {
        pthread_join(thread, nullptr);
    }

    for (queue_t* queue : queues) {
        queue_destroy(queue);
    }

    return 0;
}
