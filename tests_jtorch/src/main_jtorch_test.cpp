// THE CPP FUNCTIONALITY HERE IS TO BE TESTED AGAINST "jtorch_test.lua" SCRIPT

#include <stdlib.h>
#include <cmath>
#include <thread>
#include <iostream>
#include <limits>
#include "jtorch/torch_stage.h"
#include "jtorch/jtorch.h"
#include "jtorch/tensor.h"
#include "jtorch/spatial_convolution.h"
#include "jtorch/spatial_convolution_map.h"
#include "jtorch/spatial_lp_pooling.h"
#include "jtorch/spatial_max_pooling.h"
#include "jtorch/spatial_subtractive_normalization.h"
#include "jtorch/spatial_divisive_normalization.h"
#include "jtorch/spatial_contrastive_normalization.h"
#include "jtorch/linear.h"
#include "jtorch/reshape.h"
#include "jtorch/tanh.h"
#include "jtorch/threshold.h"
#include "jtorch/sequential.h"
#include "jtorch/parallel.h"
#include "jtorch/table.h"
#include "jtorch/join_table.h"
#include "jcl/threading/thread_pool.h"
#include "jcl/data_str/vector_managed.h"
#include "debug_util.h"
#include "file_io.h"
#include "clk/clk.h"

#if defined(WIN32) || defined(_WIN32)
  #define snprintf _snprintf_s
#endif

using namespace std;
using namespace jtorch;
using namespace jcl::threading;
using namespace jcl::math;
using namespace jcl::data_str;
using namespace jcl::file_io;
using namespace clk;

const uint32_t num_feats_in = 5;
const uint32_t num_feats_out = 10;
const uint32_t fan_in = 3;  // For SpatialConvolutionMap
const uint32_t width = 10;
const uint32_t height = 10;
const uint32_t filt_height = 5;
const uint32_t filt_width = 5;
float din[width * height * num_feats_in];
float dout[width * height * num_feats_out];

// CPU weights and biases for SpatialConvolution stage
float cweights[num_feats_out * num_feats_in * filt_height * filt_width];
float cbiases[num_feats_out];

// CPU weights and biases for Linear stage
const int32_t lin_size_in = num_feats_in * width * height;
const int32_t lin_size_out = 20;
float lweights[lin_size_in * lin_size_out];
float lbiases[lin_size_out];

void testJTorchValue(jtorch::Tensor<float>* data, const std::string& filename) {
  float* correct_data = new float[data->dataSize()];
  float* model_data = new float[data->dataSize()];
  memset(model_data, 0, sizeof(model_data[0]) * data->dataSize());
  LoadArrayFromFile<float>(correct_data, data->dataSize(), filename);
  data->getData(model_data);
  bool data_correct = true;
  for (uint32_t i = 0; i < data->dataSize() && data_correct; i++) {
    float delta = fabsf(model_data[i] - correct_data[i]) ;
    if (delta > LOOSE_EPSILON && (delta /
      std::max<float>(fabsf(correct_data[i]), EPSILON)) > 0.0001f) {
      data_correct = false;
      std::cout << "index " << i << " incorrect!: " << std::endl;
      std::cout << std::fixed << std::setprecision(15); 
      std::cout << "model_data[" << i << "] = " << model_data[i] << std::endl;
      std::cout << "correct_data[" << i << "] = " << correct_data[i] << std::endl;
    }
  }
  if (data_correct) {
    std::cout << "Test PASSED: " << filename << std::endl;
  } else {
    std::cout << "Test FAILED!: " << filename << std::endl;
  }
  delete[] model_data;
  delete[] correct_data;
}

int main(int argc, char *argv[]) {  
#if defined(_DEBUG) || defined(DEBUG)
  jcl::debug::EnableMemoryLeakChecks();
  // jcl::debug::EnableAggressiveMemoryLeakChecks();
  // jcl::debug::SetBreakPointOnAlocation(8420);
#endif

  try {
    std::cout << "Beginning jtorch tests..." << std::endl;

    const bool use_cpu = false;
    jtorch::InitJTorch("../", use_cpu);

    Tensor<float> data_in(Int3(width, height, num_feats_in));
    Tensor<float> data_out(Int3(width, height, num_feats_out));

    for (uint32_t f = 0; f < num_feats_in; f++) {
      float val = (f+1) - (float)(width * height) / 16.0f;
      for (uint32_t v = 0; v < height; v++) {
        for (uint32_t u = 0; u < width; u++) {
          din[f * width * height + v * width + u] = val;
          val += 1.0f / 8.0f;
        }
      }
    }
    data_in.setData(din);
    testJTorchValue(&data_in, "./test_data/data_in.bin");

    Sequential stages;

    // ***********************************************
    // Test Tanh
    stages.add(new Tanh());
    stages.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)stages.output, 
      "./test_data/tanh_result.bin");
    
    // ***********************************************
    // Test Threshold
    const float threshold = 0.5f;
    const float val = 0.1f;
    stages.add(new jtorch::Threshold());
    ((jtorch::Threshold*)stages.get(1))->threshold = threshold;
    ((jtorch::Threshold*)stages.get(1))->val = val;
    stages.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)stages.output, 
      "./test_data/threshold.bin");
    
    // ***********************************************
    // Test SpatialConvolutionMap --> THIS STAGE IS STILL ON THE CPU!!
    stages.add(new SpatialConvolutionMap(num_feats_in, num_feats_out, fan_in,
      filt_height, filt_width));
    for (int32_t i = 0; i < static_cast<int32_t>(num_feats_out); i++) {
      ((SpatialConvolutionMap*)stages.get(2))->biases[i] = (float)(i+1) / 
        (float)num_feats_out - 0.5f;
    }
    const float sigma_x_sq = 1.0f;
    const float sigma_y_sq = 1.0f;
    for (int32_t i = 0; i < static_cast<int32_t>(num_feats_out * fan_in); i++) {
      float scale = ((float)(i + 1) / (float)(num_feats_out * fan_in));
      for (int32_t v = 0; v < static_cast<int32_t>(filt_height); v++) {
        for (int32_t u = 0; u < static_cast<int32_t>(filt_width); u++) {
          float x = (float)u - (float)(filt_width-1) / 2.0f;
          float y = (float)v - (float)(filt_height-1) / 2.0f;
          ((SpatialConvolutionMap*)stages.get(2))->weights[i][v * filt_width + u] = 
            scale * expf(-((x*x)/(2.0f*sigma_x_sq) + (y*y)/(2.0f*sigma_y_sq)));
        }
      }
    }
    int32_t cur_filt = 0;
    for (int32_t f_out = 0; f_out < static_cast<int32_t>(num_feats_out); f_out++) {
      for (int32_t f_in = 0; f_in < static_cast<int32_t>(fan_in); f_in++) {
        ((SpatialConvolutionMap*)stages.get(2))->conn_table[f_out][f_in * 2 + 1] = cur_filt;
        int32_t cur_f_in = (f_out + f_in) % num_feats_in;
        ((SpatialConvolutionMap*)stages.get(2))->conn_table[f_out][f_in * 2] = cur_f_in;
        cur_filt++;
      }
    }
    stages.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)stages.output, 
      "./test_data/spatial_convolution_map.bin");

    // ***********************************************
    // Test SpatialConvolution
    SpatialConvolution conv(num_feats_in, num_feats_out, filt_height, filt_width);
    for (int32_t i = 0; i < static_cast<int32_t>(num_feats_out); i++) {
      cbiases[i] = (float)(i+1) / (float)num_feats_out - 0.5f;
    }
    const uint32_t filt_dim = filt_width * filt_height;
    for (int32_t fout = 0; fout < static_cast<int32_t>(num_feats_out); fout++) {
      for (int32_t fin = 0; fin < static_cast<int32_t>(num_feats_in); fin++) {
        int32_t i = fout * num_feats_out + fin;
        float scale = ((float)(i + 1) / (float)(num_feats_out * num_feats_in));
        for (int32_t v = 0; v < static_cast<int32_t>(filt_height); v++) {
          for (int32_t u = 0; u < static_cast<int32_t>(filt_width); u++) {
            float x = (float)u - (float)(filt_width-1) / 2.0f;
            float y = (float)v - (float)(filt_height-1) / 2.0f;
            cweights[fout * filt_dim * num_feats_in + fin * filt_dim + v * filt_width + u] =
              scale * expf(-((x*x)/(2.0f*sigma_x_sq) + (y*y)/(2.0f*sigma_y_sq)));
          }
        }
      }
    }
    conv.setWeights(cweights);
    conv.setBiases(cbiases);
    conv.forwardProp(*stages.get(1)->output);
    testJTorchValue((jtorch::Tensor<float>*)conv.output, 
      "./test_data/spatial_convolution.bin");
    
    // ***********************************************
    // Test SpatialLPPooling
    const float pnorm = 2;
    const int32_t pool_u = 2;
    const int32_t pool_v = 2;
    stages.add(new SpatialLPPooling(pnorm, pool_v, pool_u));
    stages.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)stages.output, 
      "./test_data/spatial_lp_pooling.bin");

    // ***********************************************
    // Test SpatialMaxPooling
    SpatialMaxPooling max_pool_stage(pool_v, pool_u);
    max_pool_stage.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)max_pool_stage.output, 
      "./test_data/spatial_max_pooling.bin");
  
    // ***********************************************
    // Test SpatialSubtractiveNormalization
    uint32_t gauss_size = 7;
    Tensor<float>* kernel = Tensor<float>::gaussian1D(gauss_size);
    // std::cout << "\tkernel1D:" << std::endl;
    // kernel->print();

    SpatialSubtractiveNormalization sub_norm_stage(*kernel);
    sub_norm_stage.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)sub_norm_stage.output, 
      "./test_data/spatial_subtractive_normalization.bin");

    // ***********************************************
    // Test SpatialDivisiveNormalization
    SpatialDivisiveNormalization div_norm_stage(*kernel);
    div_norm_stage.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)div_norm_stage.output, 
      "./test_data/spatial_divisive_normalization.bin");

    // ***********************************************
    // Test SpatialContrastiveNormalization
    const int32_t lena_width = 512;
    const int32_t lena_height = 512;
    Tensor<float> lena(Int2(lena_width, lena_height));
    float* lena_cpu = new float[lena.dataSize()];
    jcl::file_io::LoadArrayFromFile<float>(lena_cpu, 
      lena_width * lena_height, "lena_image.bin");
    lena.setData(lena_cpu);
    delete[] lena_cpu;
    uint32_t kernel_size = 7;
    Tensor<float>* kernel2 = Tensor<float>::ones1D(kernel_size);
    SpatialContrastiveNormalization cont_norm_stage(kernel2);
    cont_norm_stage.forwardProp(lena);
    float* cont_norm_output_cpu = new float[cont_norm_stage.output->dataSize()];
    ((Tensor<float>*)cont_norm_stage.output)->getData(cont_norm_output_cpu);
    jcl::file_io::SaveArrayToFile<float>(cont_norm_output_cpu, 
      lena_width * lena_height, "lena_image_processed.bin");
    std::cout << "\tSpatialContrastiveNormalization output saved: ";
    std::cout << "lena_image_processed.bin" << endl;
    delete[] cont_norm_output_cpu;

    // ***********************************************
    // Test Linear
    Sequential lin_stage;
    lin_stage.add(new Reshape());

    Linear* lin = new Linear(lin_size_in, lin_size_out);
    lin_stage.add(lin);
    // Weight matrix is M (rows = outputs) x N (columns = inputs)
    // It is stored column major with the M dimension stored contiguously
    for (int32_t n = 0; n < lin_size_in; n++) {
      for (int32_t m = 0; m < lin_size_out; m++) {
        int32_t out_i = n * lin_size_out + m;
        int32_t k = m * lin_size_in + n + 1;
        lweights[out_i] = (float)k / (float)(lin_size_in * lin_size_out);
      }
    }

    for (int32_t i = 0; i < lin_size_out; i++) {
      lbiases[i] = (float)(i+1) / (float)(lin_size_out);
    }
    lin->setBiases(lbiases);
    lin->setWeights(lweights);
    lin_stage.forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)lin_stage.output, 
      "./test_data/linear.bin");

    delete kernel;
    delete kernel2;

    // ***********************************************
    // Test Loading a model
    TorchStage* model = TorchStage::loadFromFile("./testmodel.bin");

    model->forwardProp(data_in);
    testJTorchValue((jtorch::Tensor<float>*)model->output,
      "./test_data/test_model_result.bin");

    // Some debugging if things go wrong:
    if (model->type() != SEQUENTIAL_STAGE) {
      throw std::runtime_error("main() - ERROR: Expecting sequential!");
    }

    delete model;

  } catch (std::runtime_error e) {
    std::cout << "Exception caught!" << std::endl;
    std::cout << e.what() << std::endl;
  };

  jtorch::ShutdownJTorch();

#if defined(WIN32) || defined(_WIN32)
  cout << endl;
  system("PAUSE");
#endif
}
