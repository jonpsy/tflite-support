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
#ifndef TENSORFLOW_LITE_SUPPORT_CC_TASK_AUDIO_CORE_AUDIO_FORMAT_H_
#define TENSORFLOW_LITE_SUPPORT_CC_TASK_AUDIO_CORE_AUDIO_FORMAT_H_

namespace tflite {
namespace task {
namespace audio {

// Input audio format.
class AudioFormat {
 public:
  // Audio encoding formats.
  enum class Encoding {
    kPCM8Bit,
    kPCM16Bit,
    kPCMFloat,
    kPCM24BitPacked,
    kPCM32Bit
  };

  // accessors
  int SampleRate() const { return sample_rate_; }
  int Channels() const { return channels_; }
  Encoding EncodingFormat() const { return encoding_format_; }

  // Mutators
  void SetSampleRate(int sample_rate) { sample_rate_ = sample_rate; }
  void SetChannels(int channels) { channels_ = channels; }
  void SetEncodingFormat(Encoding encoding) { encoding_format_ = encoding; }

 private:
  int sample_rate_;
  int channels_;
  Encoding encoding_format_;
};

}  // namespace audio
}  // namespace task
}  // namespace tflite

#endif  // TENSORFLOW_LITE_SUPPORT_CC_TASK_AUDIO_CORE_AUDIO_FORMAT_H_
