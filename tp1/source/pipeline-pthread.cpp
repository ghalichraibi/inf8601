#include <pthread.h>
#include <cerrno>

#include "log.h"
#include "pipeline.h"

struct worker_args {
    image_dir_t* image_dir;
    int return_value;
};

static void* worker_main(void* arg) {
    auto* args = static_cast<worker_args*>(arg);

    args->return_value = pipeline_serial(args->image_dir);
    return nullptr;
}

int pipeline_pthread(image_dir_t* image_dir) {
    pthread_t thread;
    worker_args args = {.image_dir = image_dir, .return_value = 0};

    errno = pthread_create(&thread, NULL, worker_main, &args);

    if (errno != 0) {
        LOG_ERROR_ERRNO("Failed to create pthread");
        return -1;
    }

    pthread_join(thread, nullptr);
    return args.return_value;
}
