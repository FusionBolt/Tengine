/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * License); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Copyright (c) 2021, OPEN AI LAB
 * Author:
 */

#include "pooling_kernel_ref.h"

#include "graph/tensor.h"
#include "graph/node.h"
#include "graph/graph.h"
#include "module/module.h"
#include "operator/op.h"
#include "utility/float.h"
#include "utility/sys_port.h"
#include "utility/log.h"
#include "device/cpu/cpu_node.h"
#include "device/cpu/cpu_graph.h"
#include "device/cpu/cpu_module.h"

#include <math.h>

#define HCL_POOL_MAX 0 /* Max pooling     */
#define HCL_POOL_AVG 1 /* Average pooling */


static inline float calc_sum_fp32(const float* input, int layout, int c, int h, int w, int cur_ch, int start_h,
                                  int start_w, int end_h, int end_w)
{
    float sum = 0.0f;
    for (int i = start_h; i < end_h; i++)
    {
        for (int j = start_w; j < end_w; j++)
        {
            sum += input[cur_ch * h * w + i * w + j];
        }
    }

    return sum;
}

static inline float calc_max_fp32(const float* input, int layout, int c, int h, int w, int cur_ch, int start_h,
                                  int start_w, int end_h, int end_w)
{
    float max = 0.0f;

    max = input[cur_ch * h * w + start_h * w + start_w];

    float tmp = 0.0f;
    for (int i = start_h; i < end_h; i++)
    {
        for (int j = start_w; j < end_w; j++)
        {
            tmp = input[cur_ch * h * w + i * w + j];
            max = max > tmp ? max : tmp;
        }
    }

    return max;
}

int ref_pooling_fp32(struct tensor* input_tensor, struct tensor* output_tensor,
                           struct pool_param* pool_param, int num_thread)
{
    int layout = input_tensor->layout;
    int type = input_tensor->data_type;

    int batch = input_tensor->dims[0];
    int channel = input_tensor->dims[1];
    int in_h = input_tensor->dims[2];
    int in_w = input_tensor->dims[3];
    int out_h = output_tensor->dims[2];
    int out_w = output_tensor->dims[3];

    int input_chw = channel * in_h * in_w;
    int output_chw = channel * out_h * out_w;

    int stride_h = pool_param->stride_h;
    int stride_w = pool_param->stride_w;

    int pad_h = pool_param->pad_h0;
    int pad_w = pool_param->pad_w0;

    int kernel_h = pool_param->kernel_h;
    int kernel_w = pool_param->kernel_w;

    int caffe_flavor = pool_param->caffe_flavor;
    int method = pool_param->pool_method;


    float* input = (float*)input_tensor->data;
    float* output = (float*)output_tensor->data;

    for (int n = 0; n < batch; n++)
    {
        const float* input_cur = input + n * input_chw;
        for (int c = 0; c < channel; c++)
        {
            for (int ph = 0; ph < out_h; ph++)
            {
                for (int pw = 0; pw < out_w; pw++)
                {
                    int pool_size = 1;
                    int offset = 0;
                    int h_start = ph * stride_h - pad_h;
                    int h_end = h_start + kernel_h;

                    if (h_end > in_h + pad_h)
                        h_end = in_h + pad_h;
                    int w_start = pw * stride_w - pad_w;
                    int w_end = w_start + kernel_w;

                    if (w_end > in_w + pad_w)
                        w_end = in_w + pad_w;

                    if (caffe_flavor)
                        pool_size = (h_end - h_start) * (w_end - w_start);

                    h_start = h_start > 0 ? h_start : 0;
                    w_start = w_start > 0 ? w_start : 0;
                    h_end = h_end < in_h ? h_end : in_h;
                    w_end = w_end < in_w ? w_end : in_w;

                    if (!caffe_flavor)
                        pool_size = (h_end - h_start) * (w_end - w_start);
                        
                    offset = n * output_chw + c * out_h * out_w + ph * out_w + pw;

                    if (method == HCL_POOL_MAX)
                    {
                        float max = calc_max_fp32(input_cur, layout, channel, in_h, in_w, c, h_start, w_start,
                                                    h_end, w_end);
                        output[offset] = max;
                    }
                    else if (method == HCL_POOL_AVG)
                    {
                        float sum = calc_sum_fp32(input_cur, layout, channel, in_h, in_w, c, h_start, w_start,
                                                    h_end, w_end);
                        output[offset] = sum / pool_size;
                    }
                    else
                        return -1;
                }
            }
        }
    }

    return 0;
}
