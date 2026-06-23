#pragma once

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

static inline tflite::MicroMutableOpResolver<7> create_model_resolver(void)
{
    tflite::MicroMutableOpResolver<7> resolver;
    resolver.AddReshape();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddAdd();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddMean();
    return resolver;
}
