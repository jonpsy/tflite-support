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

#include "tensorflow_lite_support/c/task/vision/image_classifier.h"

#include <string.h>

#include "tensorflow/lite/core/shims/cc/shims_test_util.h"
#include "tensorflow_lite_support/c/task/processor/classification_result.h"
#include "tensorflow_lite_support/c/task/vision/core/frame_buffer.h"
#include "tensorflow_lite_support/cc/port/gmock.h"
#include "tensorflow_lite_support/cc/port/gtest.h"
#include "tensorflow_lite_support/cc/port/status_matchers.h"
#include "tensorflow_lite_support/cc/test/test_utils.h"
#include "tensorflow_lite_support/examples/task/vision/desktop/utils/image_utils.h"

namespace tflite {
namespace task {
namespace vision {
namespace {

using ::tflite::support::StatusOr;
using ::tflite::task::JoinPath;

constexpr char kTestDataDirectory[] =
    "tensorflow_lite_support/cc/test/testdata/task/vision/";
// Quantized model.
constexpr char kMobileNetQuantizedWithMetadata[] =
    "mobilenet_v1_0.25_224_quant.tflite";

StatusOr<ImageData> LoadImage(const char* image_name) {
  return DecodeImageFromFile(
      JoinPath("./" /*test src dir*/, kTestDataDirectory, image_name));
}

class ImageClassifierFromOptionsTest : public tflite_shims::testing::Test {};

TEST_F(ImageClassifierFromOptionsTest, FailsWithMissingModelPathAndError) {
  TfLiteImageClassifierOptions options = {{{0}}};
  TfLiteError* error = nullptr;
  TfLiteImageClassifier* image_classifier =
      TfLiteImageClassifierFromOptions(&options, &error);
  EXPECT_EQ(image_classifier, nullptr);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->code, kInvalidArgumentError);
  EXPECT_NE(error->message, nullptr);
  TfLiteErrorDelete(error);
}

TEST_F(ImageClassifierFromOptionsTest, FailsWithMissingModelPathAndNullError) {
  TfLiteImageClassifierOptions options = {{{0}}};
  TfLiteImageClassifier* image_classifier =
      TfLiteImageClassifierFromOptions(&options, nullptr);
  EXPECT_EQ(image_classifier, nullptr);
}

TEST_F(ImageClassifierFromOptionsTest, SucceedsWithModelPath) {
  std::string model_path = JoinPath("./" /*test src dir*/, kTestDataDirectory,
                                    kMobileNetQuantizedWithMetadata);
  TfLiteImageClassifierOptions options = {{{0}}};
  options.base_options.model_file.file_path = model_path.data();
  TfLiteImageClassifier* image_classifier =
      TfLiteImageClassifierFromOptions(&options, nullptr);

  EXPECT_NE(image_classifier, nullptr);
  TfLiteImageClassifierDelete(image_classifier);
}

TEST_F(ImageClassifierFromOptionsTest, SucceedsWithNumberOfThreads) {
  std::string model_path = JoinPath("./" /*test src dir*/, kTestDataDirectory,
                                    kMobileNetQuantizedWithMetadata);
  TfLiteImageClassifierOptions options = {{{0}}};
  TfLiteError* error = nullptr;
  options.base_options.model_file.file_path = model_path.data();
  options.base_options.compute_settings.cpu_settings.num_threads = 3;
  TfLiteImageClassifier* image_classifier =
      TfLiteImageClassifierFromOptions(&options, &error);

  EXPECT_NE(image_classifier, nullptr);
  EXPECT_EQ(error, nullptr);
  TfLiteImageClassifierDelete(image_classifier);
  if (error) TfLiteErrorDelete(error);
}

TEST_F(ImageClassifierFromOptionsTest,
       FailsWithClassNameBlackListAndClassNameWhiteList) {
  std::string model_path = JoinPath("./" /*test src dir*/, kTestDataDirectory,
                                    kMobileNetQuantizedWithMetadata);

  TfLiteImageClassifierOptions options = {{{0}}};
  options.base_options.model_file.file_path = model_path.data();

  const char* class_name_blacklist[] = {"brambling"};
  options.classification_options.class_name_blacklist.list =
      class_name_blacklist;
  options.classification_options.class_name_blacklist.length = 1;

  const char* class_name_whitelist[] = {"cheeseburger"};
  options.classification_options.class_name_whitelist.list =
      class_name_whitelist;
  options.classification_options.class_name_whitelist.length = 1;

  TfLiteError* error = nullptr;
  TfLiteImageClassifier* image_classifier =
      TfLiteImageClassifierFromOptions(&options, &error);

  EXPECT_EQ(image_classifier, nullptr);
  if (image_classifier) TfLiteImageClassifierDelete(image_classifier);

  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->code, kInvalidArgumentError);
  EXPECT_NE(error->message, nullptr);
  TfLiteErrorDelete(error);
}

class ImageClassifierClassifyTest : public tflite_shims::testing::Test {
 protected:
  void SetUp() override {
    std::string model_path = JoinPath("./" /*test src dir*/, kTestDataDirectory,
                                      kMobileNetQuantizedWithMetadata);

    TfLiteImageClassifierOptions options = {{{0}}};
    options.base_options.model_file.file_path = model_path.data();
    image_classifier = TfLiteImageClassifierFromOptions(&options, nullptr);
    ASSERT_NE(image_classifier, nullptr);
  }

  void TearDown() override { TfLiteImageClassifierDelete(image_classifier); }
  TfLiteImageClassifier* image_classifier;
};

TEST_F(ImageClassifierClassifyTest, SucceedsWithImageData) {
  SUPPORT_ASSERT_OK_AND_ASSIGN(ImageData image_data,
                               LoadImage("burger-224.png"));

  TfLiteFrameBuffer frame_buffer = {
      .format = kRGB,
      .dimension = {.width = image_data.width, .height = image_data.height},
      .buffer = image_data.pixel_data};

  TfLiteError* error = nullptr;
  TfLiteClassificationResult* classification_result =
      TfLiteImageClassifierClassify(image_classifier, &frame_buffer, &error);

  ImageDataFree(&image_data);

  EXPECT_EQ(error, nullptr);
  if (error) TfLiteErrorDelete(error);

  ASSERT_NE(classification_result, nullptr);
  EXPECT_GE(classification_result->size, 1);
  EXPECT_NE(classification_result->classifications, nullptr);
  EXPECT_GE(classification_result->classifications->size, 1);
  EXPECT_NE(classification_result->classifications->categories, nullptr);

  // TODO(prianka): check score and labels`

  TfLiteClassificationResultDelete(classification_result);
}

TEST_F(ImageClassifierClassifyTest, SucceedsWithRoiWithinImageBounds) {
  SUPPORT_ASSERT_OK_AND_ASSIGN(ImageData image_data,
                               LoadImage("burger-224.png"));

  TfLiteFrameBuffer frame_buffer = {
      .format = kRGB,
      .dimension = {.width = image_data.width, .height = image_data.height},
      .buffer = image_data.pixel_data};

  TfLiteBoundingBox bounding_box = {
      .origin_x = 0, .origin_y = 0, .width = 100, .height = 100};
  TfLiteError* error = nullptr;
  TfLiteClassificationResult* classification_result =
      TfLiteImageClassifierClassifyWithRoi(image_classifier, &frame_buffer,
                                           &bounding_box, &error);

  ImageDataFree(&image_data);

  EXPECT_EQ(error, nullptr);
  if (error) TfLiteErrorDelete(error);

  ASSERT_NE(classification_result, nullptr);
  EXPECT_GE(classification_result->size, 1);
  EXPECT_NE(classification_result->classifications, nullptr);
  EXPECT_GE(classification_result->classifications->size, 1);
  EXPECT_NE(classification_result->classifications->categories, nullptr);
  // TODO(prianka): check score and labels`

  TfLiteClassificationResultDelete(classification_result);
}

TEST_F(ImageClassifierClassifyTest, FailsWithRoiOutsideImageBounds) {
  SUPPORT_ASSERT_OK_AND_ASSIGN(ImageData image_data,
                               LoadImage("burger-224.png"));

  TfLiteFrameBuffer frame_buffer = {
      .format = kRGB,
      .dimension = {.width = image_data.width, .height = image_data.height},
      .buffer = image_data.pixel_data};

  TfLiteBoundingBox bounding_box = {
      .origin_x = 0, .origin_y = 0, .width = 250, .height = 250};
  TfLiteError* error = nullptr;
  TfLiteClassificationResult* classification_result =
      TfLiteImageClassifierClassifyWithRoi(image_classifier, &frame_buffer,
                                           &bounding_box, &error);

  ImageDataFree(&image_data);

  EXPECT_NE(error, nullptr);
  EXPECT_EQ(error->code, kError);
  EXPECT_NE(error->message, nullptr);
  TfLiteErrorDelete(error);

  EXPECT_EQ(classification_result, nullptr);
  if (classification_result)
    TfLiteClassificationResultDelete(classification_result);
}

TEST(ImageClassifierWithUserDefinedOptionsClassifyTest,
     SucceedsWithClassNameBlackList) {
  const char* blacklisted_label_name = "cheeseburger";
  std::string model_path = JoinPath("./" /*test src dir*/, kTestDataDirectory,
                                    kMobileNetQuantizedWithMetadata);

  TfLiteImageClassifierOptions options = {{{0}}};
  options.base_options.model_file.file_path = model_path.data();

  const char* class_name_blacklist[] = {blacklisted_label_name};
  options.classification_options.class_name_blacklist.list =
      class_name_blacklist;
  options.classification_options.class_name_blacklist.length = 1;

  TfLiteImageClassifier* image_classifier =
      TfLiteImageClassifierFromOptions(&options, nullptr);
  ASSERT_NE(image_classifier, nullptr);

  SUPPORT_ASSERT_OK_AND_ASSIGN(ImageData image_data,
                               LoadImage("burger-224.png"));

  TfLiteFrameBuffer frame_buffer = {
      .format = kRGB,
      .dimension = {.width = image_data.width, .height = image_data.height},
      .buffer = image_data.pixel_data};

  TfLiteClassificationResult* classification_result =
      TfLiteImageClassifierClassify(image_classifier, &frame_buffer, nullptr);

  ImageDataFree(&image_data);

  ASSERT_NE(classification_result, nullptr);
  EXPECT_GE(classification_result->size, 1);
  EXPECT_NE(classification_result->classifications, nullptr);
  EXPECT_GE(classification_result->classifications->size, 1);
  EXPECT_NE(classification_result->classifications->categories, nullptr);
  EXPECT_NE(strcmp(classification_result->classifications->categories[0].label,
                   blacklisted_label_name),
            0);

  if (image_classifier) TfLiteImageClassifierDelete(image_classifier);

  TfLiteClassificationResultDelete(classification_result);
}

TEST(ImageClassifierWithUserDefinedOptionsClassifyTest,
     SucceedsWithClassNameWhiteList) {
  const char* whitelisted_label_name = "cheeseburger";
  std::string model_path = JoinPath("./" /*test src dir*/, kTestDataDirectory,
                                    kMobileNetQuantizedWithMetadata)
                               .data();

  TfLiteImageClassifierOptions options = {{{0}}};
  options.base_options.model_file.file_path = model_path.data();

  const char* class_name_whitelist[] = {whitelisted_label_name};
  options.classification_options.class_name_whitelist.list =
      class_name_whitelist;
  options.classification_options.class_name_whitelist.length = 1;

  TfLiteImageClassifier* image_classifier =
      TfLiteImageClassifierFromOptions(&options, nullptr);
  ASSERT_NE(image_classifier, nullptr);

  SUPPORT_ASSERT_OK_AND_ASSIGN(ImageData image_data,
                               LoadImage("burger-224.png"));

  TfLiteFrameBuffer frame_buffer = {
      .format = kRGB,
      .dimension = {.width = image_data.width, .height = image_data.height},
      .buffer = image_data.pixel_data};

  TfLiteClassificationResult* classification_result =
      TfLiteImageClassifierClassify(image_classifier, &frame_buffer, nullptr);

  ImageDataFree(&image_data);

  ASSERT_NE(classification_result, nullptr);
  EXPECT_GE(classification_result->size, 1);
  EXPECT_NE(classification_result->classifications, nullptr);
  EXPECT_GE(classification_result->classifications->size, 1);
  EXPECT_NE(classification_result->classifications->categories, nullptr);
  EXPECT_EQ(strcmp(classification_result->classifications->categories[0].label,
                   whitelisted_label_name),
            0);

  if (image_classifier) TfLiteImageClassifierDelete(image_classifier);

  TfLiteClassificationResultDelete(classification_result);
}

}  // namespace
}  // namespace vision
}  // namespace task
}  // namespace tflite
