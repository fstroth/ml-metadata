/* Copyright 2019 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "ml_metadata/metadata_store/metadata_access_object_test.h"

#include <memory>

#include "gflags/gflags.h"
#include <gmock/gmock.h>
#include "absl/time/time.h"
#include "ml_metadata/metadata_store/test_util.h"
#include "ml_metadata/proto/metadata_store.pb.h"
#include "tensorflow/core/lib/core/status_test_util.h"

namespace ml_metadata {
namespace testing {
namespace {

using ::ml_metadata::testing::ParseTextProtoOrDie;
using ::testing::UnorderedElementsAre;

TEST_P(MetadataAccessObjectTest, InitMetadataSourceCheckSchemaVersion) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  int64 schema_version;
  TF_ASSERT_OK(metadata_access_object_->GetSchemaVersion(&schema_version));
  EXPECT_EQ(schema_version,
            metadata_access_object_->query_config().schema_version());
}

TEST_P(MetadataAccessObjectTest, InitMetadataSourceIfNotExists) {
  // creates the schema and insert some records
  TF_EXPECT_OK(metadata_access_object_->InitMetadataSourceIfNotExists());
  ArtifactType want_type =
      ParseTextProtoOrDie<ArtifactType>("name: 'test_type'");
  int64 type_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  // all schema exists, the methods does nothing, check the stored type
  TF_EXPECT_OK(metadata_access_object_->InitMetadataSourceIfNotExists());
  ArtifactType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type_id, &type));
  EXPECT_THAT(type, EqualsProto(want_type));
}

TEST_P(MetadataAccessObjectTest, InitMetadataSourceIfNotExistsErrorDataLoss) {
  // creates the schema and insert some records
  TF_EXPECT_OK(metadata_access_object_->InitMetadataSourceIfNotExists());

  {
    // delete some table
    RecordSet record_set;
    TF_EXPECT_OK(metadata_access_object_->metadata_source()->ExecuteQuery(
        "DROP TABLE IF EXISTS `Type`;", &record_set));
    tensorflow::Status s =
        metadata_access_object_->InitMetadataSourceIfNotExists();
    EXPECT_EQ(s.code(), tensorflow::error::DATA_LOSS);
  }

  // reset the database by drop and recreate all tables
  TF_EXPECT_OK(metadata_access_object_->InitMetadataSource());

  {
    // drop a table.
    RecordSet record_set;
    TF_ASSERT_OK(metadata_access_object_->metadata_source()->ExecuteQuery(
        "DROP TABLE `Artifact`;", &record_set));
    tensorflow::Status s =
        metadata_access_object_->InitMetadataSourceIfNotExists();
    EXPECT_EQ(s.code(), tensorflow::error::DATA_LOSS);
  }
}

TEST_P(MetadataAccessObjectTest, InitMetadataSourceSchemaVersionMismatch) {
  // creates the schema and insert some records
  TF_EXPECT_OK(metadata_access_object_->InitMetadataSourceIfNotExists());
  {
    // delete the schema version
    RecordSet record_set;
    TF_EXPECT_OK(metadata_access_object_->metadata_source()->ExecuteQuery(
        "DELETE FROM `MLMDEnv`;", &record_set));
    tensorflow::Status s =
        metadata_access_object_->InitMetadataSourceIfNotExists();
    EXPECT_EQ(s.code(), tensorflow::error::DATA_LOSS);
  }

  // reset the database by drop and recreate all tables
  TF_EXPECT_OK(metadata_access_object_->InitMetadataSource());
  {
    // Change the `schema_version` to be a newer version.
    // fails precondition, as older library cannot work with newer db.
    RecordSet record_set;
    TF_EXPECT_OK(metadata_access_object_->metadata_source()->ExecuteQuery(
        "UPDATE `MLMDEnv` SET `schema_version` = `schema_version` + 1;",
        &record_set));
    tensorflow::Status s =
        metadata_access_object_->InitMetadataSourceIfNotExists();
    EXPECT_EQ(s.code(), tensorflow::error::FAILED_PRECONDITION);
  }
}

TEST_P(MetadataAccessObjectTest, CreateType) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type1 = ParseTextProtoOrDie<ArtifactType>("name: 'test_type'");
  int64 type1_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type1, &type1_id));

  ArtifactType type2 = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type2'
    properties { key: 'property_1' value: STRING })");
  int64 type2_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type2, &type2_id));
  EXPECT_NE(type1_id, type2_id);

  ExecutionType type3 = ParseTextProtoOrDie<ExecutionType>(
      R"(name: 'test_type'
         properties { key: 'property_2' value: INT }
         input_type: { any: {} }
         output_type: { none: {} }
      )");
  int64 type3_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type3, &type3_id));
  EXPECT_NE(type1_id, type3_id);
  EXPECT_NE(type2_id, type3_id);

  ContextType type4 = ParseTextProtoOrDie<ContextType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: STRING })");
  int64 type4_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type4, &type4_id));
  EXPECT_NE(type1_id, type4_id);
  EXPECT_NE(type2_id, type4_id);
  EXPECT_NE(type3_id, type4_id);
}

TEST_P(MetadataAccessObjectTest, CreateTypeError) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  {
    ArtifactType wrong_type;
    int64 type_id;
    // Types must at least have a name.
    EXPECT_EQ(metadata_access_object_->CreateType(wrong_type, &type_id).code(),
              tensorflow::error::INVALID_ARGUMENT);
  }
  {
    ArtifactType wrong_type = ParseTextProtoOrDie<ArtifactType>(R"(
      name: 'test_type2'
      properties { key: 'property_1' value: UNKNOWN })");
    int64 type_id;
    // Properties must have type either STRING, DOUBLE, or INT. UNKNOWN
    // is not allowed.
    EXPECT_EQ(metadata_access_object_->CreateType(wrong_type, &type_id).code(),
              tensorflow::error::INVALID_ARGUMENT);
  }
}

TEST_P(MetadataAccessObjectTest, UpdateType) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type1 = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'type1'
    properties { key: 'stored_property' value: STRING })");
  int64 type1_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type1, &type1_id));

  ExecutionType type2 = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'type2'
    properties { key: 'stored_property' value: STRING })");
  int64 type2_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type2, &type2_id));

  ContextType type3 = ParseTextProtoOrDie<ContextType>(R"(
    name: 'type3'
    properties { key: 'stored_property' value: STRING })");
  int64 type3_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type3, &type3_id));

  ArtifactType want_type1;
  want_type1.set_id(type1_id);
  want_type1.set_name("type1");
  (*want_type1.mutable_properties())["stored_property"] = STRING;
  (*want_type1.mutable_properties())["new_property"] = INT;
  TF_EXPECT_OK(metadata_access_object_->UpdateType(want_type1));

  ArtifactType got_type1;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type1_id, &got_type1));
  EXPECT_THAT(want_type1, EqualsProto(got_type1));

  // update properties may not include all existing properties
  ExecutionType want_type2;
  want_type2.set_name("type2");
  (*want_type2.mutable_properties())["new_property"] = DOUBLE;
  TF_EXPECT_OK(metadata_access_object_->UpdateType(want_type2));

  ExecutionType got_type2;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type2_id, &got_type2));
  want_type2.set_id(type2_id);
  (*want_type2.mutable_properties())["stored_property"] = STRING;
  EXPECT_THAT(want_type2, EqualsProto(got_type2));

  // update context type
  ContextType want_type3;
  want_type3.set_name("type3");
  (*want_type3.mutable_properties())["new_property"] = STRING;
  TF_EXPECT_OK(metadata_access_object_->UpdateType(want_type3));
  ContextType got_type3;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type3_id, &got_type3));
  want_type3.set_id(type3_id);
  (*want_type3.mutable_properties())["stored_property"] = STRING;
  EXPECT_THAT(want_type3, EqualsProto(got_type3));
}

TEST_P(MetadataAccessObjectTest, UpdateTypeError) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'stored_type'
    properties { key: 'stored_property' value: STRING })");
  int64 type_id;
  TF_EXPECT_OK(metadata_access_object_->CreateType(type, &type_id));
  {
    ArtifactType type_without_name;
    EXPECT_EQ(metadata_access_object_->UpdateType(type_without_name).code(),
              tensorflow::error::INVALID_ARGUMENT);
  }
  {
    ArtifactType type_with_wrong_id;
    type_with_wrong_id.set_name("stored_type");
    type_with_wrong_id.set_id(type_id + 1);
    EXPECT_EQ(metadata_access_object_->UpdateType(type_with_wrong_id).code(),
              tensorflow::error::INVALID_ARGUMENT);
  }
  {
    ArtifactType type_with_modified_property_type;
    type_with_modified_property_type.set_id(type_id);
    type_with_modified_property_type.set_name("stored_type");
    (*type_with_modified_property_type
          .mutable_properties())["stored_property"] = INT;
    EXPECT_EQ(
        metadata_access_object_->UpdateType(type_with_modified_property_type)
            .code(),
        tensorflow::error::ALREADY_EXISTS);
  }
  {
    ArtifactType type_with_unknown_type_property;
    type_with_unknown_type_property.set_id(type_id);
    type_with_unknown_type_property.set_name("stored_type");
    (*type_with_unknown_type_property.mutable_properties())["new_property"] =
        UNKNOWN;
    EXPECT_EQ(
        metadata_access_object_->UpdateType(type_with_unknown_type_property)
            .code(),
        tensorflow::error::INVALID_ARGUMENT);
  }
}

TEST_P(MetadataAccessObjectTest, FindTypeById) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType want_type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  ArtifactType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type_id, &type));
  EXPECT_THAT(type, EqualsProto(want_type));

  // type_id is for an artifact type, not an execution/context type.
  ExecutionType execution_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &execution_type).code(),
      tensorflow::error::NOT_FOUND);
  ContextType context_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &context_type).code(),
      tensorflow::error::NOT_FOUND);
}

TEST_P(MetadataAccessObjectTest, FindTypeByIdContext) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ContextType want_type = ParseTextProtoOrDie<ContextType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  ContextType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type_id, &type));
  EXPECT_THAT(type, EqualsProto(want_type));

  // type_id is for a context type, not an artifact/execution type.
  ArtifactType artifact_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &artifact_type).code(),
      tensorflow::error::NOT_FOUND);
  ExecutionType execution_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &execution_type).code(),
      tensorflow::error::NOT_FOUND);
}

TEST_P(MetadataAccessObjectTest, FindTypeByIdExecution) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType want_type = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
    input_type: { any: {} }
    output_type: { none: {} }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  ExecutionType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type_id, &type));
  EXPECT_THAT(type, EqualsProto(want_type));

  // This type_id is an execution type, not an artifact/context type.
  ArtifactType artifact_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &artifact_type).code(),
      tensorflow::error::NOT_FOUND);
  ContextType context_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &context_type).code(),
      tensorflow::error::NOT_FOUND);
}

TEST_P(MetadataAccessObjectTest, FindTypeByIdExecutionUnicode) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType want_type;
  want_type.set_name("пример_типа");
  (*want_type.mutable_properties())["привет"] = INT;
  (*want_type.mutable_input_type()
        ->mutable_dict()
        ->mutable_properties())["пример"]
      .mutable_any();
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  ExecutionType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type_id, &type));
  EXPECT_THAT(type, EqualsProto(want_type));

  // This type_id is an execution type, not an artifact/context type.
  ArtifactType artifact_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &artifact_type).code(),
      tensorflow::error::NOT_FOUND);
  ContextType context_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &context_type).code(),
      tensorflow::error::NOT_FOUND);
}

// Test if an execution type can be stored without input_type and output_type.
TEST_P(MetadataAccessObjectTest, FindTypeByIdExecutionNoSignature) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType want_type = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  ExecutionType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeById(type_id, &type));
  EXPECT_THAT(type, EqualsProto(want_type));

  // This type_id is an execution type, not an artifact/context type.
  ArtifactType artifact_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &artifact_type).code(),
      tensorflow::error::NOT_FOUND);
  ContextType context_type;
  EXPECT_EQ(
      metadata_access_object_->FindTypeById(type_id, &context_type).code(),
      tensorflow::error::NOT_FOUND);
}

TEST_P(MetadataAccessObjectTest, FindTypeByName) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType want_type = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
    input_type: { any: {} }
    output_type: { none: {} }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  ExecutionType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeByName("test_type", &type));
  EXPECT_THAT(type, EqualsProto(want_type));

  // The type with this name is an execution type, not an artifact/context type.
  ArtifactType artifact_type;
  EXPECT_EQ(metadata_access_object_->FindTypeByName("test_type", &artifact_type)
                .code(),
            tensorflow::error::NOT_FOUND);
  ContextType context_type;
  EXPECT_EQ(metadata_access_object_->FindTypeByName("test_type", &context_type)
                .code(),
            tensorflow::error::NOT_FOUND);
}

// Test if an execution type can be stored without input_type and output_type.
TEST_P(MetadataAccessObjectTest, FindTypeByNameNoSignature) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType want_type = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type, &type_id));
  want_type.set_id(type_id);

  ExecutionType type;
  TF_EXPECT_OK(metadata_access_object_->FindTypeByName("test_type", &type));
  EXPECT_THAT(type, EqualsProto(want_type));

  // The type with this name is an execution type, not an artifact/context type.
  ArtifactType artifact_type;
  EXPECT_EQ(metadata_access_object_->FindTypeByName("test_type", &artifact_type)
                .code(),
            tensorflow::error::NOT_FOUND);
  ContextType context_type;
  EXPECT_EQ(metadata_access_object_->FindTypeByName("test_type", &context_type)
                .code(),
            tensorflow::error::NOT_FOUND);
}

TEST_P(MetadataAccessObjectTest, FindAllArtifactTypes) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType want_type_1 = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type_1'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
    properties { key: 'property_4' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type_1, &type_id));
  want_type_1.set_id(type_id);

  ArtifactType want_type_2 = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type_2'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
    properties { key: 'property_5' value: STRING }
  )");
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type_2, &type_id));
  want_type_2.set_id(type_id);

  // No properties.
  ArtifactType want_type_3 = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'no_properties_type'
  )");
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type_3, &type_id));
  want_type_3.set_id(type_id);

  std::vector<ArtifactType> got_types;
  TF_EXPECT_OK(metadata_access_object_->FindTypes(&got_types));
  EXPECT_THAT(got_types, UnorderedElementsAre(EqualsProto(want_type_1),
                                              EqualsProto(want_type_2),
                                              EqualsProto(want_type_3)));
}

TEST_P(MetadataAccessObjectTest, FindAllExecutionTypes) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType want_type_1 = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type_1'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
    properties { key: 'property_4' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type_1, &type_id));
  want_type_1.set_id(type_id);

  ExecutionType want_type_2 = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type_2'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
    properties { key: 'property_5' value: STRING }
  )");
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type_2, &type_id));
  want_type_2.set_id(type_id);

  // No properties.
  ExecutionType want_type_3 = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'no_properties_type'
  )");
  TF_ASSERT_OK(metadata_access_object_->CreateType(want_type_3, &type_id));
  want_type_3.set_id(type_id);

  std::vector<ExecutionType> got_types;
  TF_EXPECT_OK(metadata_access_object_->FindTypes(&got_types));
  EXPECT_THAT(got_types, UnorderedElementsAre(EqualsProto(want_type_1),
                                              EqualsProto(want_type_2),
                                              EqualsProto(want_type_3)));
}

TEST_P(MetadataAccessObjectTest, CreateArtifact) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type_with_predefined_property'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Artifact artifact = ParseTextProtoOrDie<Artifact>(R"(
    uri: 'testuri://testing/uri'
    properties {
      key: 'property_1'
      value: { int_value: 3 }
    }
    properties {
      key: 'property_2'
      value: { double_value: 3.0 }
    }
    properties {
      key: 'property_3'
      value: { string_value: '3' }
    }
  )");
  artifact.set_type_id(type_id);

  int64 artifact1_id = -1;
  TF_EXPECT_OK(
      metadata_access_object_->CreateArtifact(artifact, &artifact1_id));
  int64 artifact2_id = -1;
  TF_EXPECT_OK(
      metadata_access_object_->CreateArtifact(artifact, &artifact2_id));
  EXPECT_NE(artifact1_id, artifact2_id);
}

TEST_P(MetadataAccessObjectTest, CreateArtifactWithCustomProperty) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type_with_custom_property'
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Artifact artifact = ParseTextProtoOrDie<Artifact>(R"(
    uri: 'testuri://testing/uri'
    custom_properties {
      key: 'custom_property_1'
      value: { int_value: 3 }
    }
    custom_properties {
      key: 'custom_property_2'
      value: { double_value: 3.0 }
    }
    custom_properties {
      key: 'custom_property_3'
      value: { string_value: '3' }
    }
  )");
  artifact.set_type_id(type_id);

  int64 artifact1_id, artifact2_id;
  TF_EXPECT_OK(
      metadata_access_object_->CreateArtifact(artifact, &artifact1_id));
  EXPECT_EQ(artifact1_id, 1);
  TF_EXPECT_OK(
      metadata_access_object_->CreateArtifact(artifact, &artifact2_id));
  EXPECT_EQ(artifact2_id, 2);
}

TEST_P(MetadataAccessObjectTest, CreateArtifactError) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());

  // unknown type specified
  Artifact artifact;
  int64 artifact_id;
  tensorflow::Status s =
      metadata_access_object_->CreateArtifact(artifact, &artifact_id);
  EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);

  artifact.set_type_id(1);
  EXPECT_EQ(
      metadata_access_object_->CreateArtifact(artifact, &artifact_id).code(),
      tensorflow::error::NOT_FOUND);

  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type_disallow_custom_property'
    properties { key: 'property_1' value: INT }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  // type mismatch
  Artifact artifact3;
  artifact3.set_type_id(type_id);
  (*artifact3.mutable_properties())["property_1"].set_string_value("3");
  int64 artifact3_id;
  EXPECT_EQ(
      metadata_access_object_->CreateArtifact(artifact3, &artifact3_id).code(),
      tensorflow::error::INVALID_ARGUMENT);
}

TEST_P(MetadataAccessObjectTest, FindArtifactById) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Artifact want_artifact = ParseTextProtoOrDie<Artifact>(R"(
    uri: 'testuri://testing/uri'
    properties {
      key: 'property_1'
      value: { int_value: 3 }
    }
    properties {
      key: 'property_2'
      value: { double_value: 3.0 }
    }
    properties {
      key: 'property_3'
      value: { string_value: '3' }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { string_value: '5' }
    }
  )");
  want_artifact.set_type_id(type_id);

  int64 artifact_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(want_artifact, &artifact_id));
  want_artifact.set_id(artifact_id);

  Artifact artifact;
  TF_EXPECT_OK(
      metadata_access_object_->FindArtifactById(artifact_id, &artifact));
  EXPECT_THAT(artifact, EqualsProto(want_artifact));
}

TEST_P(MetadataAccessObjectTest, FindAllArtifacts) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Artifact want_artifact1 = ParseTextProtoOrDie<Artifact>(R"(
    uri: 'testuri://testing/uri'
    properties {
      key: 'property_1'
      value: { int_value: 3 }
    }
    properties {
      key: 'property_2'
      value: { double_value: 3.0 }
    }
    properties {
      key: 'property_3'
      value: { string_value: '3' }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { string_value: '5' }
    }
  )");
  want_artifact1.set_type_id(type_id);

  int64 artifact1_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(want_artifact1, &artifact1_id));
  want_artifact1.set_id(artifact1_id);

  Artifact want_artifact2 = want_artifact1;
  int64 artifact2_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(want_artifact2, &artifact2_id));
  want_artifact2.set_id(artifact2_id);
  ASSERT_NE(artifact1_id, artifact2_id);

  std::vector<Artifact> artifacts;
  TF_EXPECT_OK(metadata_access_object_->FindArtifacts(&artifacts));
  EXPECT_EQ(artifacts.size(), 2);
  EXPECT_THAT(artifacts[0], EqualsProto(want_artifact1));
  EXPECT_THAT(artifacts[1], EqualsProto(want_artifact2));
}

TEST_P(MetadataAccessObjectTest, FindArtifactsByTypeIds) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>("name: 'test_type'");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));
  Artifact want_artifact1 =
      ParseTextProtoOrDie<Artifact>("uri: 'testuri://testing/uri1'");
  want_artifact1.set_type_id(type_id);
  int64 artifact1_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(want_artifact1, &artifact1_id));
  want_artifact1.set_id(artifact1_id);

  Artifact want_artifact2 =
      ParseTextProtoOrDie<Artifact>("uri: 'testuri://testing/uri2'");
  want_artifact2.set_type_id(type_id);
  int64 artifact2_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(want_artifact2, &artifact2_id));
  want_artifact2.set_id(artifact2_id);

  ArtifactType type2 = ParseTextProtoOrDie<ArtifactType>("name: 'test_type2'");
  int64 type2_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type2_id));
  Artifact artifact3;
  artifact3.set_type_id(type2_id);
  int64 artifact3_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(artifact3, &artifact3_id));

  std::vector<Artifact> artifacts;
  TF_EXPECT_OK(
      metadata_access_object_->FindArtifactsByTypeId(type_id, &artifacts));
  EXPECT_EQ(artifacts.size(), 2);
  EXPECT_THAT(artifacts[0], EqualsProto(want_artifact1));
  EXPECT_THAT(artifacts[1], EqualsProto(want_artifact2));
}

TEST_P(MetadataAccessObjectTest, FindArtifactsByURI) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>("name: 'test_type'");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));
  Artifact want_artifact1 =
      ParseTextProtoOrDie<Artifact>("uri: 'testuri://testing/uri1'");
  want_artifact1.set_type_id(type_id);
  int64 artifact1_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(want_artifact1, &artifact1_id));
  want_artifact1.set_id(artifact1_id);

  Artifact artifact2 =
      ParseTextProtoOrDie<Artifact>("uri: 'testuri://testing/uri2'");
  artifact2.set_type_id(type_id);
  int64 artifact2_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(artifact2, &artifact2_id));
  artifact2.set_id(artifact2_id);

  std::vector<Artifact> artifacts;
  TF_EXPECT_OK(metadata_access_object_->FindArtifactsByURI(
      "testuri://testing/uri1", &artifacts));
  ASSERT_EQ(artifacts.size(), 1);
  EXPECT_THAT(artifacts[0], EqualsProto(want_artifact1));
}

TEST_P(MetadataAccessObjectTest, UpdateArtifact) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Artifact stored_artifact = ParseTextProtoOrDie<Artifact>(R"(
    uri: 'testuri://testing/uri'
    properties {
      key: 'property_1'
      value: { int_value: 3 }
    }
    properties {
      key: 'property_3'
      value: { string_value: '3' }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { string_value: '5' }
    }
  )");
  stored_artifact.set_type_id(type_id);
  int64 artifact_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateArtifact(stored_artifact, &artifact_id));

  // update `property_1`, add `property_2`, and drop `property_3`
  // change the value type of `custom_property_1`
  Artifact want_artifact = ParseTextProtoOrDie<Artifact>(R"(
    uri: 'testuri://changed/uri'
    properties {
      key: 'property_1'
      value: { int_value: 5 }
    }
    properties {
      key: 'property_2'
      value: { double_value: 3.0 }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { int_value: 3 }
    }
  )");
  want_artifact.set_id(artifact_id);
  want_artifact.set_type_id(type_id);
  TF_EXPECT_OK(metadata_access_object_->UpdateArtifact(want_artifact));

  Artifact artifact;
  TF_EXPECT_OK(
      metadata_access_object_->FindArtifactById(artifact_id, &artifact));
  EXPECT_THAT(artifact, EqualsProto(want_artifact));
}

TEST_P(MetadataAccessObjectTest, UpdateArtifactError) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType type = ParseTextProtoOrDie<ArtifactType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Artifact artifact = ParseTextProtoOrDie<Artifact>(R"(
    uri: 'testuri://testing/uri'
    properties {
      key: 'property_1'
      value: { int_value: 3 }
    }
  )");
  artifact.set_type_id(type_id);
  int64 artifact_id;
  TF_ASSERT_OK(metadata_access_object_->CreateArtifact(artifact, &artifact_id));
  artifact.set_id(artifact_id);

  // no artifact id given
  Artifact wrong_artifact;
  tensorflow::Status s =
      metadata_access_object_->UpdateArtifact(wrong_artifact);
  EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);

  // artifact id cannot be found
  int64 different_id = artifact_id + 1;
  wrong_artifact.set_id(different_id);
  s = metadata_access_object_->UpdateArtifact(wrong_artifact);
  EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);

  // type_id if given is not aligned with the stored one
  wrong_artifact.set_id(artifact_id);
  int64 different_type_id = type_id + 1;
  wrong_artifact.set_type_id(different_type_id);
  s = metadata_access_object_->UpdateArtifact(wrong_artifact);
  EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);

  // artifact has unknown property
  wrong_artifact.clear_type_id();
  (*wrong_artifact.mutable_properties())["unknown_property"].set_int_value(1);
  s = metadata_access_object_->UpdateArtifact(wrong_artifact);
  EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);
}

TEST_P(MetadataAccessObjectTest, CreateAndFindExecution) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType type = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type_with_predefined_property'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Execution execution1 = ParseTextProtoOrDie<Execution>(R"(
    properties {
      key: 'property_1'
      value: { int_value: 3 }
    }
    properties {
      key: 'property_3'
      value: { string_value: '3' }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { int_value: 3 }
    }
  )");
  execution1.set_type_id(type_id);

  int64 execution1_id = -1;
  TF_EXPECT_OK(
      metadata_access_object_->CreateExecution(execution1, &execution1_id));
  execution1.set_id(execution1_id);

  ExecutionType name_only_type = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type_with_no_property'
  )");
  int64 type2_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(name_only_type, &type2_id));
  Execution execution2;
  execution2.set_type_id(type2_id);

  int64 execution2_id = -1;
  TF_EXPECT_OK(
      metadata_access_object_->CreateExecution(execution2, &execution2_id));
  execution2.set_id(execution2_id);

  EXPECT_NE(execution1_id, execution2_id);

  Execution want_execution1;
  TF_EXPECT_OK(metadata_access_object_->FindExecutionById(execution1_id,
                                                          &want_execution1));
  EXPECT_THAT(execution1, EqualsProto(want_execution1));

  Execution want_execution2;
  TF_EXPECT_OK(metadata_access_object_->FindExecutionById(execution2_id,
                                                          &want_execution2));
  EXPECT_THAT(execution2, EqualsProto(want_execution2));

  std::vector<Execution> executions;
  TF_EXPECT_OK(metadata_access_object_->FindExecutions(&executions));
  EXPECT_EQ(executions.size(), 2);
  EXPECT_THAT(executions[0], EqualsProto(want_execution1));
  EXPECT_THAT(executions[1], EqualsProto(want_execution2));

  std::vector<Execution> type1_executions;
  TF_EXPECT_OK(metadata_access_object_->FindExecutionsByTypeId(
      type_id, &type1_executions));
  EXPECT_EQ(type1_executions.size(), 1);
  EXPECT_THAT(type1_executions[0], EqualsProto(want_execution1));
}

TEST_P(MetadataAccessObjectTest, UpdateExecution) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType type = ParseTextProtoOrDie<ExecutionType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: DOUBLE }
    properties { key: 'property_3' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Execution stored_execution = ParseTextProtoOrDie<Execution>(R"(
    properties {
      key: 'property_3'
      value: { string_value: '3' }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { string_value: '5' }
    }
  )");
  stored_execution.set_type_id(type_id);
  int64 execution_id;
  TF_ASSERT_OK(metadata_access_object_->CreateExecution(stored_execution,
                                                        &execution_id));

  // add `property_1` and update `property_3`, and drop `custom_property_1`
  Execution want_execution = ParseTextProtoOrDie<Execution>(R"(
    properties {
      key: 'property_1'
      value: { int_value: 5 }
    }
    properties {
      key: 'property_3'
      value: { string_value: '5' }
    }
  )");
  want_execution.set_id(execution_id);
  want_execution.set_type_id(type_id);
  TF_EXPECT_OK(metadata_access_object_->UpdateExecution(want_execution));

  Execution execution;
  TF_EXPECT_OK(
      metadata_access_object_->FindExecutionById(execution_id, &execution));
  EXPECT_THAT(execution, EqualsProto(want_execution));
}

TEST_P(MetadataAccessObjectTest, CreateAndFindContext) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ContextType type1 = ParseTextProtoOrDie<ContextType>(R"(
    name: 'test_type_with_predefined_property'
    properties { key: 'property_1' value: INT }
  )");
  int64 type1_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type1, &type1_id));

  ContextType type2 = ParseTextProtoOrDie<ContextType>(R"(
    name: 'test_type_with_no_property'
  )");
  int64 type2_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type2, &type2_id));

  // Creates two contexts of different types
  Context context1 = ParseTextProtoOrDie<Context>(R"(
    name: "my_context1"
    properties {
      key: 'property_1'
      value: { int_value: 3 }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { int_value: 3 }
    }
  )");
  context1.set_type_id(type1_id);
  int64 context1_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateContext(context1, &context1_id));
  context1.set_id(context1_id);

  Context context2;
  context2.set_type_id(type2_id);
  context2.set_name("my_context2");
  int64 context2_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateContext(context2, &context2_id));
  context2.set_id(context2_id);

  EXPECT_NE(context1_id, context2_id);

  // Find contexts
  Context got_context1;
  TF_EXPECT_OK(
      metadata_access_object_->FindContextById(context1_id, &got_context1));
  EXPECT_THAT(got_context1, EqualsProto(context1));

  std::vector<Context> got_contexts;
  TF_EXPECT_OK(metadata_access_object_->FindContexts(&got_contexts));
  EXPECT_EQ(got_contexts.size(), 2);
  EXPECT_THAT(got_contexts[0], EqualsProto(context1));
  EXPECT_THAT(got_contexts[1], EqualsProto(context2));

  std::vector<Context> got_type2_contexts;
  TF_EXPECT_OK(metadata_access_object_->FindContextsByTypeId(
      type2_id, &got_type2_contexts));
  EXPECT_EQ(got_type2_contexts.size(), 1);
  EXPECT_THAT(got_type2_contexts[0], EqualsProto(context2));
}

TEST_P(MetadataAccessObjectTest, CreateContextError) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  Context context;
  int64 context_id;

  // unknown type specified
  EXPECT_EQ(metadata_access_object_->CreateContext(context, &context_id).code(),
            tensorflow::error::INVALID_ARGUMENT);

  context.set_type_id(1);
  EXPECT_EQ(metadata_access_object_->CreateContext(context, &context_id).code(),
            tensorflow::error::NOT_FOUND);

  ContextType type = ParseTextProtoOrDie<ContextType>(R"(
    name: 'test_type_disallow_custom_property'
    properties { key: 'property_1' value: INT }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  // type mismatch
  context.set_type_id(type_id);
  (*context.mutable_properties())["property_1"].set_string_value("3");
  EXPECT_EQ(metadata_access_object_->CreateContext(context, &context_id).code(),
            tensorflow::error::INVALID_ARGUMENT);

  // empty name
  (*context.mutable_properties())["property_1"].set_int_value(3);
  EXPECT_EQ(metadata_access_object_->CreateContext(context, &context_id).code(),
            tensorflow::error::INVALID_ARGUMENT);

  // duplicated name
  context.set_name("test context name");
  TF_EXPECT_OK(metadata_access_object_->CreateContext(context, &context_id));
  Context context_copy = context;
  EXPECT_EQ(
      metadata_access_object_->CreateContext(context_copy, &context_id).code(),
      tensorflow::error::ALREADY_EXISTS);
}

TEST_P(MetadataAccessObjectTest, UpdateContext) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ContextType type = ParseTextProtoOrDie<ContextType>(R"(
    name: 'test_type'
    properties { key: 'property_1' value: INT }
    properties { key: 'property_2' value: STRING }
  )");
  int64 type_id;
  TF_ASSERT_OK(metadata_access_object_->CreateType(type, &type_id));

  Context context1 = ParseTextProtoOrDie<Context>(R"(
    name: "before update name"
    properties {
      key: 'property_1'
      value: { int_value: 2 }
    }
    custom_properties {
      key: 'custom_property_1'
      value: { string_value: '5' }
    }
  )");
  context1.set_type_id(type_id);
  int64 context_id;
  TF_ASSERT_OK(metadata_access_object_->CreateContext(context1, &context_id));

  // add `property_2` and update `property_1`, and drop `custom_property_1`
  Context want_context = ParseTextProtoOrDie<Context>(R"(
    name: "after update name"
    properties {
      key: 'property_1'
      value: { int_value: 5 }
    }
    properties {
      key: 'property_2'
      value: { string_value: 'test' }
    }
  )");
  want_context.set_id(context_id);
  want_context.set_type_id(type_id);
  TF_EXPECT_OK(metadata_access_object_->UpdateContext(want_context));

  Context context;
  TF_EXPECT_OK(metadata_access_object_->FindContextById(context_id, &context));

  EXPECT_THAT(context, EqualsProto(want_context));
}

TEST_P(MetadataAccessObjectTest, CreateAndUseAssociation) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ExecutionType execution_type =
      ParseTextProtoOrDie<ExecutionType>("name: 'execution_type'");
  ContextType context_type =
      ParseTextProtoOrDie<ContextType>("name: 'context_type'");
  int64 execution_type_id, context_type_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(execution_type, &execution_type_id));
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(context_type, &context_type_id));

  Execution execution;
  execution.set_type_id(execution_type_id);
  (*execution.mutable_custom_properties())["custom"].set_int_value(3);
  Context context = ParseTextProtoOrDie<Context>("name: 'context_instance'");
  context.set_type_id(context_type_id);

  int64 execution_id, context_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateExecution(execution, &execution_id));
  execution.set_id(execution_id);
  TF_ASSERT_OK(metadata_access_object_->CreateContext(context, &context_id));
  context.set_id(context_id);

  Association association;
  association.set_execution_id(execution_id);
  association.set_context_id(context_id);

  int64 association_id;
  TF_EXPECT_OK(
      metadata_access_object_->CreateAssociation(association, &association_id));

  std::vector<Context> got_contexts;
  TF_EXPECT_OK(metadata_access_object_->FindContextsByExecution(execution_id,
                                                                &got_contexts));
  ASSERT_EQ(got_contexts.size(), 1);
  EXPECT_THAT(got_contexts[0], EqualsProto(context));

  std::vector<Execution> got_executions;
  TF_EXPECT_OK(metadata_access_object_->FindExecutionsByContext(
      context_id, &got_executions));
  ASSERT_EQ(got_executions.size(), 1);
  EXPECT_THAT(got_executions[0], EqualsProto(execution));

  std::vector<Artifact> got_artifacts;
  TF_EXPECT_OK(metadata_access_object_->FindArtifactsByContext(context_id,
                                                               &got_artifacts));
  EXPECT_EQ(got_artifacts.size(), 0);
}

TEST_P(MetadataAccessObjectTest, CreateAssociationError) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  Association association;
  int64 association_id;
  // no context id
  EXPECT_EQ(
      metadata_access_object_->CreateAssociation(association, &association_id)
          .code(),
      tensorflow::error::INVALID_ARGUMENT);
  // no execution id
  association.set_context_id(100);
  EXPECT_EQ(
      metadata_access_object_->CreateAssociation(association, &association_id)
          .code(),
      tensorflow::error::INVALID_ARGUMENT);
  // the context or execution cannot be found
  association.set_execution_id(100);
  EXPECT_EQ(
      metadata_access_object_->CreateAssociation(association, &association_id)
          .code(),
      tensorflow::error::INVALID_ARGUMENT);

  // duplicated association
  ExecutionType execution_type =
      ParseTextProtoOrDie<ExecutionType>("name: 'execution_type'");
  ContextType context_type =
      ParseTextProtoOrDie<ContextType>("name: 'context_type'");
  int64 execution_type_id, context_type_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(execution_type, &execution_type_id));
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(context_type, &context_type_id));
  Execution execution;
  execution.set_type_id(execution_type_id);
  Context context = ParseTextProtoOrDie<Context>("name: 'context_instance'");
  context.set_type_id(context_type_id);
  int64 execution_id, context_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateExecution(execution, &execution_id));
  TF_ASSERT_OK(metadata_access_object_->CreateContext(context, &context_id));
  association.set_execution_id(execution_id);
  association.set_context_id(context_id);

  // first insertion succeeds
  TF_EXPECT_OK(
      metadata_access_object_->CreateAssociation(association, &association_id));
  // second insertion fails
  EXPECT_EQ(
      metadata_access_object_->CreateAssociation(association, &association_id)
          .code(),
      tensorflow::error::ALREADY_EXISTS);
}

TEST_P(MetadataAccessObjectTest, CreateAndUseAttribution) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType artifact_type =
      ParseTextProtoOrDie<ArtifactType>("name: 'artifact_type'");
  ContextType context_type =
      ParseTextProtoOrDie<ContextType>("name: 'context_type'");
  int64 artifact_type_id, context_type_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(artifact_type, &artifact_type_id));
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(context_type, &context_type_id));

  Artifact artifact;
  artifact.set_uri("testuri");
  artifact.set_type_id(artifact_type_id);
  (*artifact.mutable_custom_properties())["custom"].set_string_value("str");
  Context context = ParseTextProtoOrDie<Context>("name: 'context_instance'");
  context.set_type_id(context_type_id);

  int64 artifact_id, context_id;
  TF_ASSERT_OK(metadata_access_object_->CreateArtifact(artifact, &artifact_id));
  artifact.set_id(artifact_id);
  TF_ASSERT_OK(metadata_access_object_->CreateContext(context, &context_id));
  context.set_id(context_id);

  Attribution attribution;
  attribution.set_artifact_id(artifact_id);
  attribution.set_context_id(context_id);

  int64 attribution_id;
  TF_EXPECT_OK(
      metadata_access_object_->CreateAttribution(attribution, &attribution_id));

  std::vector<Context> got_contexts;
  TF_EXPECT_OK(metadata_access_object_->FindContextsByArtifact(artifact_id,
                                                               &got_contexts));
  ASSERT_EQ(got_contexts.size(), 1);
  EXPECT_THAT(got_contexts[0], EqualsProto(context));

  std::vector<Artifact> got_artifacts;
  TF_EXPECT_OK(metadata_access_object_->FindArtifactsByContext(context_id,
                                                               &got_artifacts));
  ASSERT_EQ(got_artifacts.size(), 1);
  EXPECT_THAT(got_artifacts[0], EqualsProto(artifact));

  std::vector<Execution> got_executions;
  TF_EXPECT_OK(metadata_access_object_->FindExecutionsByContext(
      context_id, &got_executions));
  EXPECT_EQ(got_executions.size(), 0);
}

// TODO(huimiao) Refactoring the test by setting up the types in utility methods
TEST_P(MetadataAccessObjectTest, CreateAndFindEvent) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType artifact_type;
  artifact_type.set_name("test_artifact_type");
  int64 artifact_type_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(artifact_type, &artifact_type_id));
  ExecutionType execution_type;
  execution_type.set_name("test_execution_type");
  int64 execution_type_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(execution_type, &execution_type_id));
  Artifact input_artifact;
  input_artifact.set_type_id(artifact_type_id);
  int64 input_artifact_id;
  TF_ASSERT_OK(metadata_access_object_->CreateArtifact(input_artifact,
                                                       &input_artifact_id));

  Artifact output_artifact;
  output_artifact.set_type_id(artifact_type_id);
  int64 output_artifact_id;
  TF_ASSERT_OK(metadata_access_object_->CreateArtifact(output_artifact,
                                                       &output_artifact_id));

  Execution execution;
  execution.set_type_id(execution_type_id);
  int64 execution_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateExecution(execution, &execution_id));

  // event1 with event paths
  Event event1 = ParseTextProtoOrDie<Event>("type: INPUT");
  event1.set_artifact_id(input_artifact_id);
  event1.set_execution_id(execution_id);
  event1.set_milliseconds_since_epoch(12345);
  event1.mutable_path()->add_steps()->set_index(1);
  event1.mutable_path()->add_steps()->set_key("key");
  int64 event1_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateEvent(event1, &event1_id));

  // event2 with optional fields
  Event event2 = ParseTextProtoOrDie<Event>("type: OUTPUT");
  event2.set_artifact_id(output_artifact_id);
  event2.set_execution_id(execution_id);
  int64 event2_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateEvent(event2, &event2_id));

  EXPECT_NE(event1_id, -1);
  EXPECT_NE(event2_id, -1);
  EXPECT_NE(event1_id, event2_id);

  // query the executions
  std::vector<Event> events_with_input_artifact;
  TF_EXPECT_OK(metadata_access_object_->FindEventsByArtifact(
      input_artifact_id, &events_with_input_artifact));
  EXPECT_EQ(events_with_input_artifact.size(), 1);
  EXPECT_THAT(events_with_input_artifact[0], EqualsProto(event1));

  std::vector<Event> events_with_output_artifact;
  TF_EXPECT_OK(metadata_access_object_->FindEventsByArtifact(
      output_artifact_id, &events_with_output_artifact));
  EXPECT_EQ(events_with_output_artifact.size(), 1);
  event2.set_milliseconds_since_epoch(
      events_with_output_artifact[0].milliseconds_since_epoch());
  EXPECT_THAT(events_with_output_artifact[0], EqualsProto(event2));

  std::vector<Event> events_with_execution;
  TF_EXPECT_OK(metadata_access_object_->FindEventsByExecution(
      execution_id, &events_with_execution));
  EXPECT_EQ(events_with_execution.size(), 2);
}

TEST_P(MetadataAccessObjectTest, CreateEventError) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());

  // no artifact id
  {
    Event event;
    int64 event_id;
    tensorflow::Status s =
        metadata_access_object_->CreateEvent(event, &event_id);
    EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);
  }

  // no execution id
  {
    Event event;
    int64 event_id;
    event.set_artifact_id(1);
    tensorflow::Status s =
        metadata_access_object_->CreateEvent(event, &event_id);
    EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);
  }

  // no event type
  {
    Event event;
    int64 event_id;
    event.set_artifact_id(1);
    event.set_execution_id(1);
    tensorflow::Status s =
        metadata_access_object_->CreateEvent(event, &event_id);
    EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);
  }

  // artifact or execution cannot be found
  {
    ArtifactType artifact_type;
    artifact_type.set_name("test_artifact_type");
    int64 artifact_type_id;
    TF_ASSERT_OK(
        metadata_access_object_->CreateType(artifact_type, &artifact_type_id));
    Artifact artifact;
    artifact.set_type_id(artifact_type_id);
    int64 artifact_id;
    TF_ASSERT_OK(
        metadata_access_object_->CreateArtifact(artifact, &artifact_id));

    Event event;
    int64 event_id;
    event.set_artifact_id(artifact_id);
    int64 unknown_id = 12345;
    event.set_execution_id(unknown_id);
    tensorflow::Status s =
        metadata_access_object_->CreateEvent(event, &event_id);
    EXPECT_EQ(s.code(), tensorflow::error::INVALID_ARGUMENT);
  }
}

TEST_P(MetadataAccessObjectTest, PutEventsWithPaths) {
  TF_ASSERT_OK(metadata_access_object_->InitMetadataSource());
  ArtifactType artifact_type;
  artifact_type.set_name("test_artifact_type");
  int64 artifact_type_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(artifact_type, &artifact_type_id));
  ExecutionType execution_type;
  execution_type.set_name("test_execution_type");
  int64 execution_type_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateType(execution_type, &execution_type_id));
  Artifact input_artifact;
  input_artifact.set_type_id(artifact_type_id);
  int64 input_artifact_id;
  TF_ASSERT_OK(metadata_access_object_->CreateArtifact(input_artifact,
                                                       &input_artifact_id));

  Artifact output_artifact;
  output_artifact.set_type_id(artifact_type_id);
  int64 output_artifact_id;
  TF_ASSERT_OK(metadata_access_object_->CreateArtifact(output_artifact,
                                                       &output_artifact_id));

  Execution execution;
  execution.set_type_id(execution_type_id);
  int64 execution_id;
  TF_ASSERT_OK(
      metadata_access_object_->CreateExecution(execution, &execution_id));

  // event1 with event paths
  Event event1 = ParseTextProtoOrDie<Event>("type: INPUT");
  event1.set_artifact_id(input_artifact_id);
  event1.set_execution_id(execution_id);
  event1.set_milliseconds_since_epoch(12345);
  event1.mutable_path()->add_steps()->set_index(1);
  event1.mutable_path()->add_steps()->set_key("key");
  int64 event1_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateEvent(event1, &event1_id));

  // event2 with optional fields
  Event event2 = ParseTextProtoOrDie<Event>("type: OUTPUT");
  event2.set_artifact_id(output_artifact_id);
  event2.set_execution_id(execution_id);
  event2.mutable_path()->add_steps()->set_index(2);
  event2.mutable_path()->add_steps()->set_key("output_key");

  int64 event2_id = -1;
  TF_EXPECT_OK(metadata_access_object_->CreateEvent(event2, &event2_id));

  EXPECT_NE(event1_id, -1);
  EXPECT_NE(event2_id, -1);
  EXPECT_NE(event1_id, event2_id);

  // query the executions
  std::vector<Event> events_with_input_artifact;
  TF_EXPECT_OK(metadata_access_object_->FindEventsByArtifact(
      input_artifact_id, &events_with_input_artifact));
  EXPECT_EQ(events_with_input_artifact.size(), 1);
  EXPECT_THAT(events_with_input_artifact[0], EqualsProto(event1));

  std::vector<Event> events_with_output_artifact;
  TF_EXPECT_OK(metadata_access_object_->FindEventsByArtifact(
      output_artifact_id, &events_with_output_artifact));
  EXPECT_EQ(events_with_output_artifact.size(), 1);
  event2.set_milliseconds_since_epoch(
      events_with_output_artifact[0].milliseconds_since_epoch());
  EXPECT_THAT(events_with_output_artifact[0], EqualsProto(event2));

  std::vector<Event> events_with_execution;
  TF_EXPECT_OK(metadata_access_object_->FindEventsByExecution(
      execution_id, &events_with_execution));
  EXPECT_EQ(events_with_execution.size(), 2);
}

TEST_P(MetadataAccessObjectTest, MigrateToCurrentLibVersion) {
  // setup the database of previous version.
  const int64 lib_version =
      metadata_access_object_->query_config().schema_version();
  for (int64 i = 1; i <= lib_version; i++) {
    ASSERT_TRUE(
        metadata_access_object_->query_config().migration_schemes().find(i) !=
        metadata_access_object_->query_config().migration_schemes().end());
    const MetadataSourceQueryConfig::MigrationScheme scheme =
        metadata_access_object_->query_config().migration_schemes().at(i);
    if (!scheme.has_upgrade_verification()) continue;
    for (const MetadataSourceQueryConfig::TemplateQuery& setup_query :
         scheme.upgrade_verification().previous_version_setup_queries()) {
      RecordSet dummy_record_set;
      TF_EXPECT_OK(metadata_access_object_->metadata_source()->ExecuteQuery(
          setup_query.query(), &dummy_record_set));
    }
    if (i > 1) continue;
    // when i = 0, it is v0.13.2. At that time, the MLMDEnv table does not
    // exist, GetSchemaVersion resolves the current version as 0.
    int64 v0_13_2_version = 100;
    TF_EXPECT_OK(metadata_access_object_->GetSchemaVersion(&v0_13_2_version));
    EXPECT_EQ(0, v0_13_2_version);
  }
  // then init the store and the migration queries runs.
  TF_EXPECT_OK(metadata_access_object_->InitMetadataSourceIfNotExists());
  // at the end state, schema version should becomes the library version and
  // all migration queries should all succeed.
  int64 curr_version = 0;
  TF_EXPECT_OK(metadata_access_object_->GetSchemaVersion(&curr_version));
  EXPECT_EQ(lib_version, curr_version);
  // check the verification queries in the previous version scheme
  const MetadataSourceQueryConfig::MigrationScheme scheme =
      metadata_access_object_->query_config().migration_schemes().at(
          lib_version);
  if (scheme.has_upgrade_verification()) {
    for (const MetadataSourceQueryConfig::TemplateQuery& verification_query :
         scheme.upgrade_verification().post_migration_verification_queries()) {
      RecordSet record_set;
      TF_EXPECT_OK(metadata_access_object_->metadata_source()->ExecuteQuery(
          verification_query.query(), &record_set));
      ASSERT_EQ(record_set.records_size(), 1);
      bool result = false;
      ASSERT_TRUE(absl::SimpleAtob(record_set.records(0).values(0), &result));
      EXPECT_TRUE(result);
    }
  }
}

}  // namespace
}  // namespace testing
}  // namespace ml_metadata

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
