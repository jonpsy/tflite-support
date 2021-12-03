/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow_lite_support/cc/task/processor/image_postprocessor.h"

namespace tflite {
namespace task {
namespace processor {

namespace {

using ::absl::StatusCode;
using ::tflite::metadata::ModelMetadataExtractor;
using ::tflite::support::CreateStatusWithPayload;
using ::tflite::support::StatusOr;
using ::tflite::support::TfLiteSupportStatus;

StatusOr<absl::optional<vision::NormalizationOptions>>
GetNormalizationOptionsIfAny(const TensorMetadata& tensor_metadata) {
  ASSIGN_OR_RETURN(
      const tflite::ProcessUnit* normalization_process_unit,
      ModelMetadataExtractor::FindFirstProcessUnit(
          tensor_metadata, tflite::ProcessUnitOptions_NormalizationOptions));
  if (normalization_process_unit == nullptr) {
    return {absl::nullopt};
  }
  const tflite::NormalizationOptions* tf_normalization_options =
      normalization_process_unit->options_as_NormalizationOptions();
  const auto mean_values = tf_normalization_options->mean();
  const auto std_values = tf_normalization_options->std();
  if (mean_values->size() != std_values->size()) {
    return CreateStatusWithPayload(
        StatusCode::kInvalidArgument,
        absl::StrCat("NormalizationOptions: expected mean and std of same "
                     "dimension, got ",
                     mean_values->size(), " and ", std_values->size(), "."),
        TfLiteSupportStatus::kMetadataInvalidProcessUnitsError);
  }
  absl::optional<vision::NormalizationOptions> normalization_options;
  if (mean_values->size() == 1) {
    normalization_options = vision::NormalizationOptions{
        .mean_values = {mean_values->Get(0), mean_values->Get(0),
                        mean_values->Get(0)},
        .std_values = {std_values->Get(0), std_values->Get(0),
                       std_values->Get(0)},
        .num_values = 1};
  } else if (mean_values->size() == 3) {
    normalization_options = vision::NormalizationOptions{
        .mean_values = {mean_values->Get(0), mean_values->Get(1),
                        mean_values->Get(2)},
        .std_values = {std_values->Get(0), std_values->Get(1),
                       std_values->Get(2)},
        .num_values = 3};
  } else {
    return CreateStatusWithPayload(
        StatusCode::kInvalidArgument,
        absl::StrCat("NormalizationOptions: only 1 or 3 mean and std "
                     "values are supported, got ",
                     mean_values->size(), "."),
        TfLiteSupportStatus::kMetadataInvalidProcessUnitsError);
  }
  return normalization_options;
}
}  // namespace

/* static */
tflite::support::StatusOr<std::unique_ptr<ImagePostprocessor>>
ImagePostprocessor::Create(core::TfLiteEngine* engine,
                           const std::initializer_list<int> output_indices,
                           const std::initializer_list<int> input_indices) { 
  ASSIGN_OR_RETURN(auto processor, Processor::Create<ImagePostprocessor>(/* num_expected_tensors = */ 1, engine, output_indices, /* requires_metadata = */ false));

  RETURN_IF_ERROR(processor->Init(input_indices));
  return processor;
}

absl::Status ImagePostprocessor::Init(const std::vector<int>& input_indices) {
  if (core::TfLiteEngine::OutputCount(engine_->interpreter()) != 1) {
    return tflite::support::CreateStatusWithPayload(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat(
            "Image segmentation models are expected to have only 1 "
            "output, found %d",
            core::TfLiteEngine::OutputCount(engine_->interpreter())),
        tflite::support::TfLiteSupportStatus::kInvalidNumOutputTensorsError);
  }

  if (GetTensor()->type != kTfLiteUInt8 && GetTensor()->type != kTfLiteFloat32) {
    return tflite::support::CreateStatusWithPayload(
        absl::StatusCode::kInvalidArgument,
        absl::StrFormat("Type mismatch for output tensor %s. Requested one "
                        "of these types: "
                        "kTfLiteUint8/kTfLiteFloat32, got %s.",
                        GetTensor()->name, TfLiteTypeGetName(GetTensor()->type)),
        tflite::support::TfLiteSupportStatus::kInvalidOutputTensorTypeError);
  }

  if (GetTensor()->dims->data[0] != 1 || GetTensor()->dims->data[3] != 3) {
    return CreateStatusWithPayload(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("The input tensor should have dimensions 1 x height x "
                     "width x 3. Got ",
                     GetTensor()->dims->data[0], " x ", GetTensor()->dims->data[1],
                     " x ", GetTensor()->dims->data[2], " x ",
                     GetTensor()->dims->data[3], "."),
        tflite::support::TfLiteSupportStatus::
            kInvalidInputTensorDimensionsError);
  }

  // Gather metadata
  const tflite::TensorMetadata* output_metadata =
      engine_->metadata_extractor()->GetOutputTensorMetadata(
          tensor_indices_.at(0));
  const tflite::TensorMetadata* input_metadata = engine_->metadata_extractor()->GetInputTensorMetadata(
      input_indices.at(0));

  // Use input metadata for normalization as fallback.
  const tflite::TensorMetadata* processing_metadata =
      GetNormalizationOptionsIfAny(*output_metadata).value().has_value() ? output_metadata : input_metadata;

  absl::optional<vision::NormalizationOptions> normalization_options;
  ASSIGN_OR_RETURN(normalization_options,
                   GetNormalizationOptionsIfAny(*processing_metadata));

  if (GetTensor()->type == kTfLiteFloat32) {
    if (!normalization_options.has_value()) {
      return CreateStatusWithPayload(
          absl::StatusCode::kNotFound,
          "Output tensor has type kTfLiteFloat32: it requires specifying "
          "NormalizationOptions metadata to preprocess output images.",
          TfLiteSupportStatus::kMetadataMissingNormalizationOptionsError);
    } else if (GetTensor()->bytes / sizeof(float) %
                   normalization_options.value().num_values !=
               0) {
      return CreateStatusWithPayload(
          StatusCode::kInvalidArgument,
          "The number of elements in the output tensor must be a multiple of "
          "the number of normalization parameters.",
          TfLiteSupportStatus::kInvalidArgumentError);
    }

    options_ = std::make_unique<vision::NormalizationOptions>(
        normalization_options.value());
  }

  return absl::OkStatus();
}

absl::StatusOr<vision::FrameBuffer> ImagePostprocessor::Postprocess() {
  has_uint8_outputs_ = GetTensor()->type == kTfLiteUInt8;
  const int kRgbPixelBytes = 3;

  vision::FrameBuffer::Dimension to_buffer_dimension = {
      GetTensor()->dims->data[2], GetTensor()->dims->data[1]};
  size_t output_byte_size =
      GetBufferByteSize(to_buffer_dimension, vision::FrameBuffer::Format::kRGB);
  std::vector<uint8> postprocessed_data(output_byte_size / sizeof(uint8), 0);

  if (has_uint8_outputs_) {  // No denormalization required.
    if (GetTensor()->bytes != output_byte_size) {
      return tflite::support::CreateStatusWithPayload(
          absl::StatusCode::kInternal,
          "Size mismatch or unsupported padding bytes between pixel data "
          "and output tensor.");
    }
    const uint8* output_data =
        core::AssertAndReturnTypedTensor<uint8>(GetTensor()).value();
    postprocessed_data.insert(postprocessed_data.begin(), &output_data[0],
                              &output_data[output_byte_size / sizeof(uint8)]);
  } else {  // Denormalize to [0, 255] range.
    if (GetTensor()->bytes / sizeof(float) != output_byte_size / sizeof(uint8)) {
      return tflite::support::CreateStatusWithPayload(
          absl::StatusCode::kInternal,
          "Size mismatch or unsupported padding bytes between pixel data "
          "and output tensor.");
    }

    uint8* denormalized_output_data = postprocessed_data.data();
    const float* output_data =
        core::AssertAndReturnTypedTensor<float>(GetTensor()).value();
    const auto norm_options = GetNormalizationOptions();

    if (norm_options.num_values == 1) {
      float mean_value = norm_options.mean_values[0];
      float std_value = norm_options.std_values[0];

      for (size_t i = 0; i < output_byte_size / sizeof(uint8);
           ++i, ++denormalized_output_data, ++output_data) {
        *denormalized_output_data = static_cast<uint8>(std::round(std::min(
            255.f, std::max(0.f, (*output_data) * std_value + mean_value))));
      }
    } else {
      for (size_t i = 0; i < output_byte_size / sizeof(uint8);
           ++i, ++denormalized_output_data, ++output_data) {
        *denormalized_output_data = static_cast<uint8>(std::round(std::min(
            255.f, 
            std::max(0.f, (*output_data) * norm_options.std_values[i % 3] +
                              norm_options.mean_values[i % 3]))));
      }
    }
  }

  vision::FrameBuffer::Plane postprocessed_plane = {
      /*buffer=*/postprocessed_data.data(),
      /*stride=*/{GetTensor()->dims->data[2] * kRgbPixelBytes, kRgbPixelBytes}};
  auto postprocessed_frame_buffer =
      vision::FrameBuffer::Create({postprocessed_plane}, to_buffer_dimension,
                                  vision::FrameBuffer::Format::kRGB,
                                  vision::FrameBuffer::Orientation::kTopLeft);
  return *postprocessed_frame_buffer.get();;
}

}  // namespace processor
}  // namespace task
}  // namespace tflite
