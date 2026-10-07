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

constexpr unsigned int MAX_IMAGES_PER_QUEUE = 4;
constexpr unsigned int N_STAGES             = 5;  // loader + scale_up + desaturate + edge_detect + saver
constexpr unsigned int N_QUEUES             = N_STAGES - 1;

// Fraction des coeurs attribuée à chaque étage, d'après le coût mesuré par image :
// load ~4 ms, scale_up ~1 ms, desaturate ~2 ms, edge_detect ~16 ms, save ~88 ms.
// save est le goulot : un worker par coeur. Les autres étages ont une marge au-delà de
// leur coût relatif (edge_detect : 16 / 88 ≈ 0,18 -> 0,5). La somme dépasse 1 : un worker
// inactif dort dans queue_pop, et le surplus absorbe les écarts de coût entre images.
// Le loader reste unique : image_dir_load_next n'est pas thread-safe.
constexpr double SCALE_UP_WEIGHT    = 0.125;
constexpr double DESATURATE_WEIGHT  = 0.125;
constexpr double EDGE_DETECT_WEIGHT = 0.5;
constexpr double SAVE_WEIGHT        = 1.0;

using filter_fn = image_t* (*)(image_t*);
using thread_fn = void* (*)(void*);

struct stage_args {
    thread_fn main_fn;
    image_dir_t* image_dir;
    queue_t* in;
    queue_t* out;
    filter_fn filter;
    unsigned int n_workers;
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

using queue_array   = std::array<queue_t*, N_QUEUES>;
using stage_array   = std::array<stage_args, N_STAGES>;
using stage_threads = std::array<std::vector<pthread_t>, N_STAGES>;

static image_t* filter_scale_up_3(image_t* image) {
    return filter_scale_up(image, 3);
}

// Nombre de workers d'un étage selon son poids (au moins un)
static unsigned int workers_from_weight(double weight) {
    const unsigned int cores = std::max(1u, std::thread::hardware_concurrency());
    return std::max(1u, static_cast<unsigned int>(cores * weight));
}

static queue_array create_queues() {
    queue_array queues;
    for (queue_t*& queue : queues) {
        queue = queue_create(MAX_IMAGES_PER_QUEUE);
    }
    return queues;
}

static void destroy_queues(const queue_array& queues) {
    for (queue_t* queue : queues) {
        queue_destroy(queue);
    }
}

static stage_array make_stages(image_dir_t* image_dir, const queue_array& queues) {
    return {{
        {.main_fn = load, .image_dir = image_dir, .out = queues[0], .n_workers = 1},
        {.main_fn   = filter,
         .in        = queues[0],
         .out       = queues[1],
         .filter    = filter_scale_up_3,
         .n_workers = workers_from_weight(SCALE_UP_WEIGHT)},
        {.main_fn   = filter,
         .in        = queues[1],
         .out       = queues[2],
         .filter    = filter_desaturate,
         .n_workers = workers_from_weight(DESATURATE_WEIGHT)},
        {.main_fn   = filter,
         .in        = queues[2],
         .out       = queues[3],
         .filter    = filter_edge_detect,
         .n_workers = workers_from_weight(EDGE_DETECT_WEIGHT)},
        {.main_fn = save, .image_dir = image_dir, .in = queues[3], .n_workers = workers_from_weight(SAVE_WEIGHT)},
    }};
}

static bool start_workers(stage_array& stages, stage_threads& threads) {
    // Consommateurs avant producteurs : si un thread est refusé, le loader n'a pas encore
    // démarré, aucune image n'entre dans le pipeline et join_stages reste propre.
    for (unsigned int i = N_STAGES; i-- > 0;) {
        for (unsigned int w = 0; w < stages[i].n_workers; w++) {
            pthread_t thread;
            errno = pthread_create(&thread, nullptr, stages[i].main_fn, &stages[i]);
            if (errno != 0) {
                LOG_ERROR_ERRNO("pthread_create");
                return false;
            }
            threads[i].push_back(thread);
        }
    }
    return true;
}

// Attend chaque étage, puis envoie une sentinelle par worker de l'étage suivant
static void join_stages(const queue_array& queues, const stage_threads& threads) {
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
}

int pipeline_pthread(image_dir_t* image_dir) {
    queue_array queues = create_queues();
    stage_array stages = make_stages(image_dir, queues);

    stage_threads threads;
    const bool started = start_workers(stages, threads);
    join_stages(queues, threads);

    destroy_queues(queues);
    return started ? 0 : -1;
}
