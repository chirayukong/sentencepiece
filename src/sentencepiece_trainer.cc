// Copyright 2018 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.!

#include "sentencepiece_trainer.h"
#include <string>

#include "builder.h"
#include "common.h"
#include "flags.h"
#include "normalizer.h"
#include "sentencepiece.pb.h"
#include "sentencepiece_model.pb.h"
#include "trainer_factory.h"
#include "util.h"

namespace sentencepiece {

// static
util::Status SentencePieceTrainer::Train(const TrainerSpec &trainer_spec) {
  NormalizerSpec normalizer_spec;
  return Train(trainer_spec, normalizer_spec);
}

// static
util::Status SentencePieceTrainer::Train(
    const TrainerSpec &trainer_spec, const NormalizerSpec &normalizer_spec) {
  auto copied_normalizer_spec = normalizer_spec;
  RETURN_IF_ERROR(
      normalizer::Builder::PopulateNormalizerSpec(&copied_normalizer_spec));

  auto trainer = TrainerFactory::Create(trainer_spec, copied_normalizer_spec);
  return trainer->Train();
}

// static
util::Status SentencePieceTrainer::SetProtoField(
    const std::string &field_name, const std::string &value,
    google::protobuf::Message *message) {
  const auto *descriptor = message->GetDescriptor();
  const auto *reflection = message->GetReflection();

  CHECK_OR_RETURN(descriptor != nullptr && reflection != nullptr)
      << "reflection is not supported.";

  const auto *field = descriptor->FindFieldByName(std::string(field_name));

  if (field == nullptr) {
    return util::StatusBuilder(util::error::NOT_FOUND)
           << "unknown field name \"" << field_name << "\" in\n"
           << descriptor->DebugString();
  }

  std::vector<std::string> values = {value};
  if (field->is_repeated()) values = string_util::Split(value, ",");

#define SET_FIELD(METHOD_TYPE, v)                    \
  if (field->is_repeated())                          \
    reflection->Add##METHOD_TYPE(message, field, v); \
  else                                               \
    reflection->Set##METHOD_TYPE(message, field, v);

#define DEFINE_SET_FIELD(PROTO_TYPE, CPP_TYPE, FUNC_PREFIX, METHOD_TYPE,       \
                         EMPTY)                                                \
  case google::protobuf::FieldDescriptor::CPPTYPE_##PROTO_TYPE: {              \
    CPP_TYPE v;                                                                \
    if (!string_util::lexical_cast(value.empty() ? EMPTY : value, &v))         \
      return util::StatusBuilder(util::error::INVALID_ARGUMENT)                \
             << "cannot parse \"" << value << "\" as \"" << field->type_name() \
             << "\".";                                                         \
    SET_FIELD(METHOD_TYPE, v);                                                 \
    break;                                                                     \
  }

  for (const auto &value : values) {
    switch (field->cpp_type()) {
      DEFINE_SET_FIELD(INT32, int32, i, Int32, "");
      DEFINE_SET_FIELD(INT64, int64, i, Int64, "");
      DEFINE_SET_FIELD(UINT32, uint32, i, UInt32, "");
      DEFINE_SET_FIELD(UINT64, uint64, i, UInt64, "");
      DEFINE_SET_FIELD(DOUBLE, double, d, Double, "");
      DEFINE_SET_FIELD(FLOAT, float, f, Float, "");
      DEFINE_SET_FIELD(BOOL, bool, b, Bool, "true");
      case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        SET_FIELD(String, value);
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
        const auto *enum_value =
            field->enum_type()->FindValueByName(string_util::ToUpper(value));
        if (enum_value == nullptr)
          return util::StatusBuilder(util::error::INVALID_ARGUMENT)
                 << "unknown enumeration value of \"" << value
                 << "\" for field \"" << field->name() << "\".";
        SET_FIELD(Enum, enum_value);
        break;
      }
      default:
        return util::StatusBuilder(util::error::UNIMPLEMENTED)
               << "proto type \"" << field->cpp_type_name()
               << "\" is not supported.";
    }
  }

  return util::OkStatus();
}

// static
util::Status SentencePieceTrainer::MergeSpecsFromArgs(
    const std::string &args, TrainerSpec *trainer_spec,
    NormalizerSpec *normalizer_spec) {
  CHECK_OR_RETURN(trainer_spec) << "`trainer_spec` must not be null.";
  CHECK_OR_RETURN(normalizer_spec) << "`normalizer_spec` must not be null.";

  if (args.empty()) return util::OkStatus();

  for (auto arg : string_util::SplitPiece(args, " ")) {
    arg.Consume("--");
    std::string key, value;
    auto pos = arg.find("=");
    if (pos == StringPiece::npos) {
      key = arg.ToString();
    } else {
      key = arg.substr(0, pos).ToString();
      value = arg.substr(pos + 1).ToString();
    }

    // Exception.
    if (key == "normalization_rule_name") {
      normalizer_spec->set_name(value);
      continue;
    }

    const auto status_train = SetProtoField(key, value, trainer_spec);
    if (status_train.ok()) continue;
    if (!util::IsNotFound(status_train)) return status_train;

    const auto status_norm = SetProtoField(key, value, normalizer_spec);
    if (status_norm.ok()) continue;
    if (!util::IsNotFound(status_norm)) return status_norm;

    // Not found both in trainer_spec and normalizer_spec.
    if (util::IsNotFound(status_train) && util::IsNotFound(status_norm)) {
      return status_train;
    }
  }

  return util::OkStatus();
}

// static
util::Status SentencePieceTrainer::Train(const std::string &args) {
  TrainerSpec trainer_spec;
  NormalizerSpec normalizer_spec;
  CHECK_OK(MergeSpecsFromArgs(args, &trainer_spec, &normalizer_spec));
  return Train(trainer_spec, normalizer_spec);
}

}  // namespace sentencepiece
