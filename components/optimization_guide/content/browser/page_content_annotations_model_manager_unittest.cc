// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/optimization_guide/content/browser/page_content_annotations_model_manager.h"

#include "base/containers/flat_map.h"
#include "base/path_service.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/optimization_guide/core/page_entities_model_executor.h"
#include "components/optimization_guide/core/test_model_info_builder.h"
#include "components/optimization_guide/core/test_optimization_guide_model_provider.h"
#include "components/optimization_guide/proto/page_topics_model_metadata.pb.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace optimization_guide {

using ::testing::FloatEq;
using ::testing::UnorderedElementsAre;

class ModelObserverTracker : public TestOptimizationGuideModelProvider {
 public:
  void AddObserverForOptimizationTargetModel(
      proto::OptimizationTarget target,
      const absl::optional<proto::Any>& model_metadata,
      OptimizationTargetModelObserver* observer) override {
    registered_model_metadata_.insert_or_assign(target, model_metadata);
  }

  bool DidRegisterForTarget(
      proto::OptimizationTarget target,
      absl::optional<proto::Any>* out_model_metadata) const {
    auto it = registered_model_metadata_.find(target);
    if (it == registered_model_metadata_.end())
      return false;
    *out_model_metadata = registered_model_metadata_.at(target);
    return true;
  }

 private:
  base::flat_map<proto::OptimizationTarget, absl::optional<proto::Any>>
      registered_model_metadata_;
};

class FakePageEntitiesModelExecutor : public PageEntitiesModelExecutor {
 public:
  explicit FakePageEntitiesModelExecutor(
      const base::flat_map<std::string, std::vector<ScoredEntityMetadata>>&
          entries,
      const base::flat_map<std::string, EntityMetadata>& entity_metadata)
      : entries_(entries), entity_metadata_(entity_metadata) {}
  ~FakePageEntitiesModelExecutor() override = default;

  void HumanReadableExecuteModelWithInput(
      const std::string& text,
      PageEntitiesMetadataModelExecutedCallback callback) override {
    auto it = entries_.find(text);
    std::move(callback).Run(
        it != entries_.end() ? absl::make_optional(it->second) : absl::nullopt);
  }

  void GetMetadataForEntityId(
      const std::string& entity_id,
      PageEntitiesModelEntityMetadataRetrievedCallback callback) override {
    auto it = entity_metadata_.find(entity_id);
    std::move(callback).Run(it != entity_metadata_.end()
                                ? absl::make_optional(it->second)
                                : absl::nullopt);
  }

 private:
  base::flat_map<std::string, std::vector<ScoredEntityMetadata>> entries_;
  base::flat_map<std::string, EntityMetadata> entity_metadata_;
};

class PageContentAnnotationsModelManagerTest : public testing::Test {
 public:
  PageContentAnnotationsModelManagerTest() {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        features::kPageContentAnnotations,
        {{"models_to_execute", "OPTIMIZATION_TARGET_PAGE_TOPICS"}});
  }
  ~PageContentAnnotationsModelManagerTest() override = default;

  void SetUp() override {
    histogram_tester_ = std::make_unique<base::HistogramTester>();

    model_observer_tracker_ = std::make_unique<ModelObserverTracker>();
    model_manager_ = std::make_unique<PageContentAnnotationsModelManager>(
        model_observer_tracker_.get());
  }

  void TearDown() override {
    model_manager_.reset();
    model_observer_tracker_.reset();
  }

  void SendPageTopicsModelToExecutor(
      const absl::optional<proto::Any>& model_metadata) {
    base::FilePath source_root_dir;
    base::PathService::Get(base::DIR_SOURCE_ROOT, &source_root_dir);
    base::FilePath model_file_path =
        source_root_dir.AppendASCII("components")
            .AppendASCII("test")
            .AppendASCII("data")
            .AppendASCII("optimization_guide")
            .AppendASCII("bert_page_topics_model.tflite");
    std::unique_ptr<ModelInfo> model_info =
        TestModelInfoBuilder()
            .SetModelFilePath(model_file_path)
            .SetModelMetadata(model_metadata)
            .Build();
    model_manager()->page_topics_model_handler_->OnModelUpdated(
        proto::OPTIMIZATION_TARGET_PAGE_TOPICS, *model_info);
    RunUntilIdle();
  }

  void SendPageTopicsV2ModelToExecutor(
      const absl::optional<proto::Any>& model_metadata) {
    model_manager()->SetUpPageTopicsV2Model(model_observer_tracker());

    base::FilePath source_root_dir;
    base::PathService::Get(base::DIR_SOURCE_ROOT, &source_root_dir);
    base::FilePath model_file_path =
        source_root_dir.AppendASCII("components")
            .AppendASCII("test")
            .AppendASCII("data")
            .AppendASCII("optimization_guide")
            .AppendASCII("bert_page_topics_model.tflite");
    std::unique_ptr<ModelInfo> model_info =
        TestModelInfoBuilder()
            .SetModelFilePath(model_file_path)
            .SetModelMetadata(model_metadata)
            .Build();
    model_manager()->on_demand_page_topics_model_executor_->OnModelUpdated(
        proto::OPTIMIZATION_TARGET_PAGE_TOPICS_V2, *model_info);
    RunUntilIdle();
  }

  void SetPageEntitiesModelExecutor(
      const base::flat_map<std::string, std::vector<ScoredEntityMetadata>>&
          entries,
      const base::flat_map<std::string, EntityMetadata>& entity_metadata) {
    model_manager()->OverridePageEntitiesModelExecutorForTesting(
        std::make_unique<FakePageEntitiesModelExecutor>(entries,
                                                        entity_metadata));
  }

  absl::optional<history::VisitContentModelAnnotations> Annotate(
      const std::string& text) {
    absl::optional<history::VisitContentModelAnnotations> content_annotations;
    base::RunLoop run_loop;
    model_manager()->Annotate(
        "sometext",
        base::BindOnce(
            [](base::RunLoop* run_loop,
               absl::optional<history::VisitContentModelAnnotations>*
                   out_content_annotations,
               const absl::optional<history::VisitContentModelAnnotations>&
                   content_annotations) {
              *out_content_annotations = content_annotations;
              run_loop->Quit();
            },
            &run_loop, &content_annotations));
    run_loop.Run();

    return content_annotations;
  }

  absl::optional<EntityMetadata> GetMetadataForEntityId(
      const std::string& entity_id) {
    absl::optional<EntityMetadata> entities_metadata;
    base::RunLoop run_loop;
    model_manager()->GetMetadataForEntityId(
        entity_id,
        base::BindOnce(
            [](base::RunLoop* run_loop,
               absl::optional<EntityMetadata>* out_entities_metadata,
               const absl::optional<EntityMetadata>& entities_metadata) {
              *out_entities_metadata = entities_metadata;
              run_loop->Quit();
            },
            &run_loop, &entities_metadata));
    run_loop.Run();

    return entities_metadata;
  }

  ModelObserverTracker* model_observer_tracker() const {
    return model_observer_tracker_.get();
  }

  PageContentAnnotationsModelManager* model_manager() const {
    return model_manager_.get();
  }

  history::VisitContentModelAnnotations GetContentModelAnnotationsFromOutput(
      const proto::PageTopicsModelMetadata& metadata,
      const std::vector<tflite::task::core::Category>& model_output) const {
    history::VisitContentModelAnnotations annotations;
    model_manager()->PopulateContentModelAnnotationsFromPageTopicsModelOutput(
        metadata, model_output, &annotations);
    return annotations;
  }

  base::HistogramTester* histogram_tester() const {
    return histogram_tester_.get();
  }

  void RunUntilIdle() { task_environment_.RunUntilIdle(); }

 private:
  content::BrowserTaskEnvironment task_environment_;

  std::unique_ptr<base::HistogramTester> histogram_tester_;
  base::test::ScopedFeatureList scoped_feature_list_;

  std::unique_ptr<ModelObserverTracker> model_observer_tracker_;
  std::unique_ptr<PageContentAnnotationsModelManager> model_manager_;
};

TEST_F(PageContentAnnotationsModelManagerTest,
       SetsUpModelsCorrectlyBasedOnFeatureParams) {
  absl::optional<proto::Any> registered_model_metadata;
  EXPECT_TRUE(model_observer_tracker()->DidRegisterForTarget(
      proto::OPTIMIZATION_TARGET_PAGE_TOPICS, &registered_model_metadata));
  EXPECT_TRUE(registered_model_metadata.has_value());
  absl::optional<proto::PageTopicsModelMetadata> page_topics_model_metadata =
      ParsedAnyMetadata<proto::PageTopicsModelMetadata>(
          *registered_model_metadata);
  EXPECT_TRUE(page_topics_model_metadata.has_value());
  EXPECT_EQ(page_topics_model_metadata->supported_output_size(), 2);
  EXPECT_THAT(
      page_topics_model_metadata->supported_output(),
      UnorderedElementsAre(proto::PAGE_TOPICS_SUPPORTED_OUTPUT_VISIBILITY,
                           proto::PAGE_TOPICS_SUPPORTED_OUTPUT_CATEGORIES));

  // The feature param did not specify page entities, so we expect for it to not
  // be requested.
  histogram_tester()->ExpectTotalCount(
      "OptimizationGuide.PageContentAnnotationsModelManager."
      "PageEntitiesModelRequested",
      0);
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetPageTopicsModelVersionNoPushedModel) {
  EXPECT_FALSE(model_manager()->GetPageTopicsModelVersion().has_value());
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetPageTopicsModelVersionFromExecutor) {
  proto::Any any_metadata;
  any_metadata.set_type_url(
      "type.googleapis.com/com.foo.PageTopicsModelMetadata");
  proto::PageTopicsModelMetadata page_topics_model_metadata;
  page_topics_model_metadata.set_version(123);
  page_topics_model_metadata.SerializeToString(any_metadata.mutable_value());
  SendPageTopicsModelToExecutor(any_metadata);

  absl::optional<int64_t> model_version =
      model_manager()->GetPageTopicsModelVersion();
  EXPECT_TRUE(model_version.has_value());
  EXPECT_EQ(model_version.value(), 123);
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetPageTopicsModelVersionFromExecutorBadMetadata) {
  proto::Any any_metadata;
  any_metadata.set_type_url(
      "type.googleapis.com/com.foo.whatevernotpagetopics");
  any_metadata.set_value("123");
  SendPageTopicsModelToExecutor(any_metadata);

  absl::optional<int64_t> model_version =
      model_manager()->GetPageTopicsModelVersion();
  EXPECT_FALSE(model_version.has_value());
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetContentModelAnnotationsFromOutputVisibilityOnly) {
  proto::PageTopicsModelMetadata model_metadata;
  model_metadata.set_version(123);
  model_metadata.mutable_output_postprocessing_params()
      ->mutable_visibility_params()
      ->set_category_name("SOMECATEGORY");

  std::vector<tflite::task::core::Category> model_output = {
      {"SOMECATEGORY", 0.4},
      {"-2", 0.3},
  };
  history::VisitContentModelAnnotations annotations =
      GetContentModelAnnotationsFromOutput(model_metadata, model_output);
  EXPECT_TRUE(annotations.categories.empty());
  EXPECT_THAT(annotations.visibility_score, FloatEq(0.6));
  EXPECT_EQ(annotations.page_topics_model_version, 123);
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetContentModelAnnotationsFromOutputVisibilityOnlyCategoryNotInOutput) {
  proto::PageTopicsModelMetadata model_metadata;
  model_metadata.set_version(123);
  model_metadata.mutable_output_postprocessing_params()
      ->mutable_visibility_params()
      ->set_category_name("SOMECATEGORY");

  std::vector<tflite::task::core::Category> model_output = {
      {"-2", 0.3},
  };
  history::VisitContentModelAnnotations annotations =
      GetContentModelAnnotationsFromOutput(model_metadata, model_output);
  EXPECT_TRUE(annotations.categories.empty());
  EXPECT_EQ(annotations.visibility_score, -1.0);
  EXPECT_EQ(annotations.page_topics_model_version, 123);
  EXPECT_TRUE(annotations.entities.empty());
}

TEST_F(
    PageContentAnnotationsModelManagerTest,
    GetContentModelAnnotationsFromOutputNonNumericAndLowWeightCategoriesPruned) {
  proto::PageTopicsModelMetadata model_metadata;
  model_metadata.set_version(123);
  auto* category_params = model_metadata.mutable_output_postprocessing_params()
                              ->mutable_category_params();
  category_params->set_max_categories(4);
  category_params->set_min_none_weight(0.8);
  category_params->set_min_category_weight(0.01);
  category_params->set_min_normalized_weight_within_top_n(0.1);

  std::vector<tflite::task::core::Category> model_output = {
      {"0", 0.0001}, {"1", 0.1}, {"SOMECATEGORY", 0.9}, {"2", 0.2}, {"3", 0.3},
  };
  history::VisitContentModelAnnotations annotations =
      GetContentModelAnnotationsFromOutput(model_metadata, model_output);
  EXPECT_THAT(annotations.categories,
              UnorderedElementsAre(
                  history::VisitContentModelAnnotations::Category("1", 10),
                  history::VisitContentModelAnnotations::Category("2", 20),
                  history::VisitContentModelAnnotations::Category("3", 30)));
  EXPECT_EQ(annotations.visibility_score, -1.0);
  EXPECT_EQ(annotations.page_topics_model_version, 123);
  EXPECT_TRUE(annotations.entities.empty());
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetContentModelAnnotationsFromOutputNoneWeightTooStrong) {
  proto::PageTopicsModelMetadata model_metadata;
  model_metadata.set_version(123);
  auto* category_params = model_metadata.mutable_output_postprocessing_params()
                              ->mutable_category_params();
  category_params->set_max_categories(4);
  category_params->set_min_none_weight(0.1);
  category_params->set_min_category_weight(0.01);
  category_params->set_min_normalized_weight_within_top_n(0.1);

  std::vector<tflite::task::core::Category> model_output = {
      {"-2", 0.9999},
      {"0", 0.3},
      {"1", 0.2},
  };
  history::VisitContentModelAnnotations annotations =
      GetContentModelAnnotationsFromOutput(model_metadata, model_output);
  EXPECT_TRUE(annotations.categories.empty());
  EXPECT_EQ(annotations.visibility_score, -1.0);
  EXPECT_EQ(annotations.page_topics_model_version, 123);
  EXPECT_TRUE(annotations.entities.empty());
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetContentModelAnnotationsFromOutputNoneInTopButNotStrongSoPruned) {
  proto::PageTopicsModelMetadata model_metadata;
  model_metadata.set_version(123);
  auto* category_params = model_metadata.mutable_output_postprocessing_params()
                              ->mutable_category_params();
  category_params->set_max_categories(4);
  category_params->set_min_none_weight(0.8);
  category_params->set_min_category_weight(0.01);
  category_params->set_min_normalized_weight_within_top_n(0.1);

  std::vector<tflite::task::core::Category> model_output = {
      {"-2", 0.1}, {"0", 0.3}, {"1", 0.2}, {"2", 0.4}, {"3", 0.05},
  };
  history::VisitContentModelAnnotations annotations =
      GetContentModelAnnotationsFromOutput(model_metadata, model_output);
  EXPECT_THAT(annotations.categories,
              UnorderedElementsAre(
                  history::VisitContentModelAnnotations::Category("0", 30),
                  history::VisitContentModelAnnotations::Category("1", 20),
                  history::VisitContentModelAnnotations::Category("2", 40)));
  EXPECT_EQ(annotations.visibility_score, -1.0);
  EXPECT_EQ(annotations.page_topics_model_version, 123);
  EXPECT_TRUE(annotations.entities.empty());
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetContentModelAnnotationsFromOutputPrunedAfterNormalization) {
  proto::PageTopicsModelMetadata model_metadata;
  model_metadata.set_version(123);
  auto* category_params = model_metadata.mutable_output_postprocessing_params()
                              ->mutable_category_params();
  category_params->set_max_categories(4);
  category_params->set_min_none_weight(0.8);
  category_params->set_min_category_weight(0.01);
  category_params->set_min_normalized_weight_within_top_n(0.25);

  std::vector<tflite::task::core::Category> model_output = {
      {"0", 0.3},
      {"1", 0.25},
      {"2", 0.4},
      {"3", 0.05},
  };
  history::VisitContentModelAnnotations annotations =
      GetContentModelAnnotationsFromOutput(model_metadata, model_output);
  EXPECT_THAT(annotations.categories,
              UnorderedElementsAre(
                  history::VisitContentModelAnnotations::Category("0", 30),
                  history::VisitContentModelAnnotations::Category("1", 25),
                  history::VisitContentModelAnnotations::Category("2", 40)));
  EXPECT_EQ(annotations.visibility_score, -1.0);
  EXPECT_EQ(annotations.page_topics_model_version, 123);
  EXPECT_TRUE(annotations.entities.empty());
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetContentModelAnnotationsFromOutputCategoriesAndVisibility) {
  proto::PageTopicsModelMetadata model_metadata;
  model_metadata.set_version(123);
  auto* category_params = model_metadata.mutable_output_postprocessing_params()
                              ->mutable_category_params();
  category_params->set_max_categories(4);
  category_params->set_min_none_weight(0.8);
  category_params->set_min_category_weight(0.01);
  category_params->set_min_normalized_weight_within_top_n(0.25);
  model_metadata.mutable_output_postprocessing_params()
      ->mutable_visibility_params()
      ->set_category_name("SOMECATEGORY");

  std::vector<tflite::task::core::Category> model_output = {
      {"0", 0.3}, {"1", 0.25}, {"2", 0.4}, {"3", 0.05}, {"SOMECATEGORY", 0.4},
  };
  history::VisitContentModelAnnotations annotations =
      GetContentModelAnnotationsFromOutput(model_metadata, model_output);
  EXPECT_THAT(annotations.categories,
              UnorderedElementsAre(
                  history::VisitContentModelAnnotations::Category("0", 30),
                  history::VisitContentModelAnnotations::Category("1", 25),
                  history::VisitContentModelAnnotations::Category("2", 40)));
  EXPECT_THAT(annotations.visibility_score, FloatEq(0.6));
  EXPECT_EQ(annotations.page_topics_model_version, 123);
}

TEST_F(PageContentAnnotationsModelManagerTest,
       AnnotateNoModelsFinishedExecuting) {
  proto::Any any_metadata;
  any_metadata.set_type_url(
      "type.googleapis.com/com.foo.PageTopicsModelMetadata");
  proto::PageTopicsModelMetadata page_topics_model_metadata;
  page_topics_model_metadata.set_version(123);
  page_topics_model_metadata.SerializeToString(any_metadata.mutable_value());
  SendPageTopicsModelToExecutor(any_metadata);

  EXPECT_FALSE(Annotate("sometext").has_value());
}

TEST_F(PageContentAnnotationsModelManagerTest, AnnotateModelNotAvailable) {
  EXPECT_FALSE(Annotate("sometext").has_value());
}

TEST_F(PageContentAnnotationsModelManagerTest,
       GetMetadataForEntityIdEntitiesAnnotatorNotInitialized) {
  EXPECT_FALSE(GetMetadataForEntityId("someid").has_value());
}

TEST_F(PageContentAnnotationsModelManagerTest, BatchAnnotate_PageTopics) {
  proto::Any any_metadata;
  any_metadata.set_type_url(
      "type.googleapis.com/com.foo.PageTopicsModelMetadata");
  proto::PageTopicsModelMetadata page_topics_model_metadata;
  page_topics_model_metadata.set_version(123);
  page_topics_model_metadata.SerializeToString(any_metadata.mutable_value());
  SendPageTopicsV2ModelToExecutor(any_metadata);

  // Running the actual model can take a while.
  base::test::ScopedRunLoopTimeout scoped_timeout(FROM_HERE, base::Seconds(60));

  base::RunLoop run_loop;
  std::vector<BatchAnnotationResult> result;
  BatchAnnotationCallback callback = base::BindOnce(
      [](base::RunLoop* run_loop,
         std::vector<BatchAnnotationResult>* out_result,
         const std::vector<BatchAnnotationResult>& in_result) {
        *out_result = in_result;
        run_loop->Quit();
      },
      &run_loop, &result);

  model_manager()->Annotate(std::move(callback), {"input"},
                            AnnotationType::kPageTopics);
  run_loop.Run();

  // TODO(crbug/1249632): Check the corresponding output once the model is being
  // run.
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].input(), "input");
  EXPECT_EQ(result[0].type(), AnnotationType::kPageTopics);
  EXPECT_EQ(result[0].status(), ExecutionStatus::kSuccess);
  EXPECT_EQ(result[0].topics(), absl::nullopt);
  EXPECT_EQ(result[0].entities(), absl::nullopt);
  EXPECT_EQ(result[0].visibility_score(), absl::nullopt);
}

TEST_F(PageContentAnnotationsModelManagerTest,
       BatchAnnotate_PageTopicsNotAvailable) {
  // Note that |SendPageTopicsV2ModelToExecutor| is not called.
  base::RunLoop run_loop;
  std::vector<BatchAnnotationResult> result;
  BatchAnnotationCallback callback = base::BindOnce(
      [](base::RunLoop* run_loop,
         std::vector<BatchAnnotationResult>* out_result,
         const std::vector<BatchAnnotationResult>& in_result) {
        *out_result = in_result;
        run_loop->Quit();
      },
      &run_loop, &result);

  model_manager()->Annotate(std::move(callback), {"input"},
                            AnnotationType::kPageTopics);
  run_loop.Run();

  // TODO(crbug/1249632): Check the corresponding output once the model is being
  // run.
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].input(), "input");
  EXPECT_EQ(result[0].type(), AnnotationType::kPageTopics);
  EXPECT_EQ(result[0].status(), ExecutionStatus::kErrorModelFileNotAvailable);
  EXPECT_EQ(result[0].topics(), absl::nullopt);
  EXPECT_EQ(result[0].entities(), absl::nullopt);
  EXPECT_EQ(result[0].visibility_score(), absl::nullopt);
}

TEST_F(PageContentAnnotationsModelManagerTest, BatchAnnotate_PageEntities) {
  base::RunLoop run_loop;
  std::vector<BatchAnnotationResult> result;
  BatchAnnotationCallback callback = base::BindOnce(
      [](base::RunLoop* run_loop,
         std::vector<BatchAnnotationResult>* out_result,
         const std::vector<BatchAnnotationResult>& in_result) {
        *out_result = in_result;
        run_loop->Quit();
      },
      &run_loop, &result);

  model_manager()->Annotate(std::move(callback), {"input"},
                            AnnotationType::kPageEntities);
  run_loop.Run();

  // TODO(crbug/1249632): Check the corresponding output once the model is being
  // run.
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].input(), "input");
  EXPECT_EQ(result[0].status(), ExecutionStatus::kErrorInternalError);
  EXPECT_EQ(result[0].topics(), absl::nullopt);
  EXPECT_EQ(result[0].entities(), absl::nullopt);
  EXPECT_EQ(result[0].visibility_score(), absl::nullopt);
}

TEST_F(PageContentAnnotationsModelManagerTest,
       BatchAnnotate_ContentVisibility) {
  base::RunLoop run_loop;
  std::vector<BatchAnnotationResult> result;
  BatchAnnotationCallback callback = base::BindOnce(
      [](base::RunLoop* run_loop,
         std::vector<BatchAnnotationResult>* out_result,
         const std::vector<BatchAnnotationResult>& in_result) {
        *out_result = in_result;
        run_loop->Quit();
      },
      &run_loop, &result);

  model_manager()->Annotate(std::move(callback), {"input"},
                            AnnotationType::kContentVisibility);
  run_loop.Run();

  // TODO(crbug/1249632): Check the corresponding output once the model is being
  // run.
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].input(), "input");
  EXPECT_EQ(result[0].status(), ExecutionStatus::kErrorInternalError);
  EXPECT_EQ(result[0].topics(), absl::nullopt);
  EXPECT_EQ(result[0].entities(), absl::nullopt);
  EXPECT_EQ(result[0].visibility_score(), absl::nullopt);
}

TEST_F(PageContentAnnotationsModelManagerTest, BatchAnnotate_CalledTwice) {
  proto::Any any_metadata;
  any_metadata.set_type_url(
      "type.googleapis.com/com.foo.PageTopicsModelMetadata");
  proto::PageTopicsModelMetadata page_topics_model_metadata;
  page_topics_model_metadata.set_version(123);
  page_topics_model_metadata.SerializeToString(any_metadata.mutable_value());
  SendPageTopicsV2ModelToExecutor(any_metadata);

  // Running the actual model can take a while.
  base::test::ScopedRunLoopTimeout scoped_timeout(FROM_HERE,
                                                  base::Seconds(120));

  base::RunLoop run_loop1;
  std::vector<BatchAnnotationResult> result1;
  BatchAnnotationCallback callback1 = base::BindOnce(
      [](base::RunLoop* run_loop,
         std::vector<BatchAnnotationResult>* out_result,
         const std::vector<BatchAnnotationResult>& in_result) {
        *out_result = in_result;
        run_loop->Quit();
      },
      &run_loop1, &result1);

  model_manager()->Annotate(std::move(callback1), {"input1"},
                            AnnotationType::kPageTopics);

  base::RunLoop run_loop2;
  std::vector<BatchAnnotationResult> result2;
  BatchAnnotationCallback callback2 = base::BindOnce(
      [](base::RunLoop* run_loop,
         std::vector<BatchAnnotationResult>* out_result,
         const std::vector<BatchAnnotationResult>& in_result) {
        *out_result = in_result;
        run_loop->Quit();
      },
      &run_loop2, &result2);

  model_manager()->Annotate(std::move(callback2), {"input2"},
                            AnnotationType::kPageEntities);

  run_loop1.Run();
  run_loop2.Run();

  // TODO(crbug/1249632): Check the corresponding output once the model is being
  // run.
  ASSERT_EQ(result1.size(), 1U);
  EXPECT_EQ(result1[0].input(), "input1");
  EXPECT_EQ(result1[0].type(), AnnotationType::kPageTopics);
  EXPECT_EQ(result1[0].status(), ExecutionStatus::kSuccess);
  EXPECT_EQ(result1[0].topics(), absl::nullopt);
  EXPECT_EQ(result1[0].entities(), absl::nullopt);
  EXPECT_EQ(result1[0].visibility_score(), absl::nullopt);
  ASSERT_EQ(result2.size(), 1U);
  EXPECT_EQ(result2[0].input(), "input2");
  EXPECT_EQ(result2[0].status(), ExecutionStatus::kErrorInternalError);
  EXPECT_EQ(result2[0].topics(), absl::nullopt);
  EXPECT_EQ(result2[0].entities(), absl::nullopt);
  EXPECT_EQ(result2[0].visibility_score(), absl::nullopt);
}

class PageContentAnnotationsModelManagerEntitiesOnlyTest
    : public PageContentAnnotationsModelManagerTest {
 public:
  PageContentAnnotationsModelManagerEntitiesOnlyTest() {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        features::kPageContentAnnotations,
        {{"models_to_execute", "OPTIMIZATION_TARGET_PAGE_ENTITIES"}});
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_F(PageContentAnnotationsModelManagerEntitiesOnlyTest,
       SetsUpModelsCorrectlyBasedOnFeatureParams) {
  // The feature param did not specify page topics, so we expect for it to not
  // be requested.
  absl::optional<proto::Any> registered_model_metadata;
  EXPECT_FALSE(model_observer_tracker()->DidRegisterForTarget(
      proto::OPTIMIZATION_TARGET_PAGE_TOPICS, &registered_model_metadata));

  // But it did specify page entities.
  histogram_tester()->ExpectUniqueSample(
      "OptimizationGuide.PageContentAnnotationsModelManager."
      "PageEntitiesModelRequested",
      true, 1);
}

TEST_F(PageContentAnnotationsModelManagerEntitiesOnlyTest,
       AnnotateNoModelsFinishedExecuting) {
  SetPageEntitiesModelExecutor(
      {{"sometext",
        {
            ScoredEntityMetadata(0.6, EntityMetadata("entity6", "entity6", {})),
            ScoredEntityMetadata(0.5, EntityMetadata("entity5", "entity5", {})),
            ScoredEntityMetadata(0.4, EntityMetadata("entity4", "entity4", {})),
            ScoredEntityMetadata(0.3, EntityMetadata("entity3", "entity3", {})),
            ScoredEntityMetadata(0.2, EntityMetadata("entity2", "entity2", {})),
        }}},
      /*entity_metadata=*/{});

  absl::optional<history::VisitContentModelAnnotations> annotations =
      Annotate("sometext");

  histogram_tester()->ExpectUniqueSample(
      "OptimizationGuide.PageContentAnnotationsModelManager."
      "PageEntitiesModelExecutionRequested",
      true, 1);

  // We expect that the page topics model will not be requested for execution.
  histogram_tester()->ExpectTotalCount(
      "OptimizationGuide.PageContentAnnotationsModelManager."
      "PageTopicsModelExecutionRequested",
      0);

  // Make sure annotations object is populated correctly.
  ASSERT_TRUE(annotations.has_value());
  EXPECT_TRUE(annotations->categories.empty());
  EXPECT_EQ(annotations->visibility_score, -1.0);
  EXPECT_THAT(
      annotations->entities,
      UnorderedElementsAre(
          history::VisitContentModelAnnotations::Category("entity6", 60),
          history::VisitContentModelAnnotations::Category("entity5", 50),
          history::VisitContentModelAnnotations::Category("entity4", 40),
          history::VisitContentModelAnnotations::Category("entity3", 30),
          history::VisitContentModelAnnotations::Category("entity2", 20)));
}

TEST_F(PageContentAnnotationsModelManagerEntitiesOnlyTest,
       GetMetadataForEntityIdEntitiesAnnotatorInitialized) {
  EntityMetadata entity_metadata;
  entity_metadata.human_readable_name = "entity1";
  entity_metadata.human_readable_categories = {
      {"category1", 0.5},
  };
  SetPageEntitiesModelExecutor(/*entries=*/{}, {
                                                   {"entity1", entity_metadata},
                                               });
  EXPECT_TRUE(GetMetadataForEntityId("entity1").has_value());
}

class PageContentAnnotationsModelManagerMultipleModelsTest
    : public PageContentAnnotationsModelManagerTest {
 public:
  PageContentAnnotationsModelManagerMultipleModelsTest() {
    scoped_feature_list_.InitAndEnableFeatureWithParameters(
        features::kPageContentAnnotations,
        {{"models_to_execute",
          "OPTIMIZATION_TARGET_PAGE_ENTITIES,OPTIMIZATION_TARGET_PAGE_"
          "TOPICS"}});
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_F(PageContentAnnotationsModelManagerMultipleModelsTest,
       AnnotateRequestBothModels) {
  SetPageEntitiesModelExecutor(
      {{"sometext",
        {
            ScoredEntityMetadata(0.6, EntityMetadata("entity6", "entity6", {})),
            ScoredEntityMetadata(0.5, EntityMetadata("entity5", "entity5", {})),
            ScoredEntityMetadata(0.4, EntityMetadata("entity4", "entity4", {})),
            ScoredEntityMetadata(0.3, EntityMetadata("entity3", "entity3", {})),
            ScoredEntityMetadata(0.2, EntityMetadata("entity2", "entity2", {})),
        }}},
      /*entity_metadata=*/{});

  Annotate("sometext");

  histogram_tester()->ExpectUniqueSample(
      "OptimizationGuide.PageContentAnnotationsModelManager."
      "PageEntitiesModelExecutionRequested",
      true, 1);

  // We expect that the page topics model will also be requested for execution.
  histogram_tester()->ExpectTotalCount(
      "OptimizationGuide.PageContentAnnotationsService.ModelAvailable", 1);
}

}  // namespace optimization_guide
