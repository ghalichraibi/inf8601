#include <pthread.h>
#include <cerrno>

#include "filter.h"
#include "log.h"
#include "pipeline.h"
#include "queue.h"

static constexpr int QUEUE_SIZE = 4;

struct shared_args {
    image_dir_t* image_dir;
    queue_t* queue;
};

static void* loader(void* arg) {
    auto* args     = static_cast<shared_args*>(arg);
    queue_t* queue = args->queue;

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

static void* processing(void* arg) {
    auto* args     = static_cast<shared_args*>(arg);
    queue_t* queue = args->queue;

    while (true) {
        auto* image1 = static_cast<image_t*>(queue_pop(queue));
        if (image1 == nullptr) {
            break;
        }

        image_t* image2 = filter_scale_up(image1, 3);
        image_destroy(image1);

        image_t* image3 = filter_desaturate(image2);
        image_destroy(image2);

        image_t* image4 = filter_edge_detect(image3);
        image_destroy(image3);

        image_dir_save(args->image_dir, image4);
        image_destroy(image4);
    }

    return nullptr;
}

int pipeline_pthread(image_dir_t* image_dir) {
    pthread_t loader_thread, processing_thread;
    queue_t* queue = queue_create(QUEUE_SIZE);

    if (queue == nullptr) {
        LOG_ERROR("Failed to create queue");
        return -1;
    }

    shared_args shared_args = {.image_dir = image_dir, .queue = queue};

    pthread_create(&loader_thread, nullptr, loader, &shared_args);
    pthread_create(&processing_thread, nullptr, processing, &shared_args);

    pthread_join(loader_thread, nullptr);
    pthread_join(processing_thread, nullptr);

    queue_destroy(queue);

    return 0;
}
