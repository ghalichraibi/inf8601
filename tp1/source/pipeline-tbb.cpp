#include <algorithm>
#include <thread>

#include <tbb/parallel_pipeline.h>

#include "filter.h"
#include "pipeline.h"

const size_t MAX_IMAGES_IN_FLIGHT   = std::max<size_t>(1, 2 * std::thread::hardware_concurrency());
constexpr unsigned int SCALE_FACTOR = 3;

int pipeline_tbb(image_dir_t* image_dir) {
    auto load =
        tbb::make_filter<void, image_t*>(tbb::filter_mode::serial_in_order, [&](tbb::flow_control& fc) -> image_t* {
            image_t* image = image_dir_load_next(image_dir);
            if (image == nullptr) {
                fc.stop();
            }
            return image;
        });

    auto scale_up = tbb::make_filter<image_t*, image_t*>(tbb::filter_mode::parallel, [](image_t* image) {
        image_t* new_image = filter_scale_up(image, SCALE_FACTOR);
        image_destroy(image);
        return new_image;
    });

    auto desaturate = tbb::make_filter<image_t*, image_t*>(tbb::filter_mode::parallel, [](image_t* image) {
        image_t* new_image = filter_desaturate(image);
        image_destroy(image);
        return new_image;
    });

    auto edge_detect = tbb::make_filter<image_t*, image_t*>(tbb::filter_mode::parallel, [](image_t* image) {
        image_t* new_image = filter_edge_detect(image);
        image_destroy(image);
        return new_image;
    });

    auto save = tbb::make_filter<image_t*, void>(tbb::filter_mode::parallel, [&](image_t* image) {
        image_dir_save(image_dir, image);
        image_destroy(image);
    });

    tbb::parallel_pipeline(MAX_IMAGES_IN_FLIGHT, load & scale_up & desaturate & edge_detect & save);
    return 0;
}
