#include "jtorch/reshape.h"
#include "jtorch/tensor.h"
#include "jcl/threading/thread.h"
#include "jcl/threading/callback.h"
#include "jcl/threading/thread_pool.h"
#include "jcl/data_str/vector_managed.h"

#define SAFE_DELETE(x) if (x != NULL) { delete x; x = NULL; }
#define SAFE_DELETE_ARR(x) if (x != NULL) { delete[] x; x = NULL; }

using namespace jcl::threading;
using namespace jcl::math;
using namespace jcl::data_str;

namespace jtorch {

  Reshape::Reshape() : TorchStage() {
    output = NULL;
  }

  Reshape::~Reshape() {
    //  SAFE_DELETE(output);
    // EDIT: output is NOT owned here, this is just a pass through.
  }

  void Reshape::init(TorchData& input)  { 
    //if (input.type() != TorchDataType::TENSOR_DATA) {
    //  throw std::wruntime_error("Reshape::init() - "
    //    "FloatTensor expected!");
    //}
    //Tensor<float>& in = (Tensor<float>&)input;
    //if (output != NULL) {

    //  if (in.dataSize() != ((Tensor<float>*)output)->dim()[0]) {
    //    // Input dimension has changed!
    //    SAFE_DELETE(output);
    //  }
    //}
    //if (output == NULL) {
    //  Int3 out_dim(in.dataSize(), 1, 1);
    //  output = new Tensor<float>(out_dim);

    //  jcl::math::Int3 local_worgroup_size;
    //}
    // EDIT: Nothing to do:  This is just a passthrough stage
  }

  void Reshape::forwardProp(TorchData& input) { 
    init(input);
    output = &input;
  }

  TorchStage* Reshape::loadFromFile(std::ifstream& file) {
    // Nothing to do for Reshape
    return new Reshape();
  }

}  // namespace jtorch