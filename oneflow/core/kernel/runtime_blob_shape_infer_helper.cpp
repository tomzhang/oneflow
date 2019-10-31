#include "oneflow/core/kernel/runtime_blob_shape_infer_helper.h"
#include "oneflow/core/register/blob.h"
#include "oneflow/core/common/cached_caller.h"
#include "oneflow/core/job/resource_desc.h"

namespace oneflow {

RuntimeBlobShapeInferHelper::RuntimeBlobShapeInferHelper(const OperatorConf& op_conf,
                                                         const KernelConf& kernel_conf,
                                                         const JobDesc* job_desc) {
  op_ = ConstructOp(op_conf, job_desc);
  auto* map = sbp_signature_.mutable_bn_in_op2sbp_parallel();
  op_->ForEachBnInOp([&](const std::string& bn_in_op) {
    bn_in_op2blob_desc_[bn_in_op].reset();
    (*map)[bn_in_op].mutable_split_parallel()->set_axis(0);
  });
  parallel_ctx_.set_parallel_id(0);
  parallel_ctx_.set_parallel_num(1);
  op_infer_cache_key_.job_desc = job_desc;
  op_infer_cache_key_.op_conf_sym = op_->GetOpConfWithoutOpNameAndLbn();
  for (const auto& ibn : op_->input_bns()) {
    op_infer_cache_key_.ibn2shape_sym[ibn] = Symbol<Shape>();
  }
  op_infer_cache_key_.dtype_signature_sym = SymbolOf(kernel_conf.dtype_signature());
}

void RuntimeBlobShapeInferHelper::UpdateInputBlobDescs7OpInferCacheKey(
    std::function<Blob*(const std::string&)> BnInOp2Blob) {
  for (const auto& ibn : op_->input_bns()) {
    const Blob* blob = BnInOp2Blob(ibn);
    if (blob == nullptr) { continue; }
    BlobDesc* blob_desc = BlobDesc4BnInOp(ibn, blob->blob_desc());
    blob_desc->mut_shape().LeftOnesExtendedAssign(blob->shape());
    op_infer_cache_key_.ibn2shape_sym[ibn] = SymbolOf(blob_desc->shape());
  }
}

BlobDesc* RuntimeBlobShapeInferHelper::BlobDesc4BnInOp(const std::string& bn_in_op,
                                                       const RtBlobDesc& rt_blob_desc) {
  BlobDesc* blob_desc = bn_in_op2blob_desc_.at(bn_in_op).get();
  if (blob_desc != nullptr) { return blob_desc; }
  blob_desc = new BlobDesc(rt_blob_desc.body(), rt_blob_desc.num_of_lod_levels(),
                           rt_blob_desc.is_body_disabled(), rt_blob_desc.is_dynamic());
  bn_in_op2blob_desc_.at(bn_in_op).reset(blob_desc);
  return blob_desc;
}

void RuntimeBlobShapeInferHelper::InferDenseShape(
    std::function<Blob*(const std::string&)> BnInOp2Blob) {
  UpdateInputBlobDescs7OpInferCacheKey(BnInOp2Blob);
  auto Infer = [&](const OpInferCacheKey& key) -> std::shared_ptr<const OpInferCacheValue> {
    auto CachedBlobDesc4BnInOp = WithResultCached([&](const std::string& bn_in_op) -> BlobDesc* {
      const Blob* blob = BnInOp2Blob(bn_in_op);
      if (blob == nullptr) { return nullptr; }
      return BlobDesc4BnInOp(bn_in_op, blob->blob_desc());
    });
    CHECK_JUST(op_->InferOutBlobDescsIf(CachedBlobDesc4BnInOp, &parallel_ctx_, &sbp_signature_,
                                        [](OpContext*) {}));
    auto* ret = new OpInferCacheValue();
    for (const auto& obn : op_->output_bns()) {
      const auto& blob_desc = bn_in_op2blob_desc_.at(obn);
      ret->obn2shape_sym[obn].reset(blob_desc->shape());
      auto* blob = BnInOp2Blob(obn);
      if (blob == nullptr) { continue; }
      CHECK_EQ(blob->data_type(), blob_desc->data_type());
      CHECK_EQ(blob->blob_desc().is_dynamic(), blob_desc->is_dynamic());
      CHECK_EQ(blob->blob_desc().is_body_disabled(), blob_desc->is_body_disabled());
    }
    return std::shared_ptr<const OpInferCacheValue>(ret);
  };
  size_t cache_size = Global<ResourceDesc>::Get()->thread_local_cache_max_size();
  const auto& shape_infer_ret = ThreadLocalCachedCall(cache_size, Infer, op_infer_cache_key_);
  const auto& obn2shape_sym = shape_infer_ret->obn2shape_sym;
  for (const auto& obn : op_->output_bns()) {
    auto* blob = BnInOp2Blob(obn);
    if (blob == nullptr) { continue; }
    if (blob->blob_desc().is_dynamic()) {
      const int64_t num_of_lod_levels = blob->blob_desc().num_of_lod_levels();
      const int64_t num_left_ones = (num_of_lod_levels == 0 ? 0 : num_of_lod_levels - 1);
      CHECK_EQ(num_left_ones, obn2shape_sym.at(obn)->NumAxes() - blob->shape().NumAxes());
      blob->dense_shape_mut_view()->LeftOnesStrippedAssign(*obn2shape_sym.at(obn));
    } else {
      CHECK(*obn2shape_sym.at(obn) == blob->static_shape());
    }
  }
}

}  // namespace oneflow