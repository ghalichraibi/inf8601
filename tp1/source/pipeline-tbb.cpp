#include <stdio.h>

#include <tbb/parallel_pipeline.h>

#include "filter.h"
#include "pipeline.h"

int pipeline_tbb(image_dir_t* image_dir) {
    tbb::parallel_pipeline(
        16, tbb::make_filter<void, image_t*>(tbb::filter_mode::serial_in_order, [&](tbb::flow_control& fc) -> image_t* {
                image_t* image = image_dir_load_next(image_dir);
                if (image == nullptr) {
                    fc.stop();
                }
                return image;
            }) & tbb::make_filter<image_t*, void>(tbb::filter_mode::serial_in_order, [&](image_t* image1) {
                image_t* image2 = filter_scale_up(image1, 3);
                image_destroy(image1);
                image_t* image3 = filter_desaturate(image2);
                image_destroy(image2);
                image_t* image4 = filter_edge_detect(image3);
                image_destroy(image3);

                image_dir_save(image_dir, image4);
                image_destroy(image4);
            }));
    return 0;
}
