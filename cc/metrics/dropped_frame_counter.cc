// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/metrics/dropped_frame_counter.h"

#include <algorithm>
#include <cmath>

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_macros.h"
#include "base/trace_event/trace_event.h"
#include "cc/metrics/frame_sorter.h"
#include "cc/metrics/total_frame_counter.h"
#include "cc/metrics/ukm_smoothness_data.h"

namespace cc {
namespace {

// The start ranges of each bucket, up to but not including the start of the
// next bucket. The last bucket contains the remaining values.
constexpr double kBucketBounds[7] = {0, 3, 6, 12, 25, 50, 75};

// Search backwards using the bucket bounds defined above.
size_t DecideSmoothnessBucket(double pdf) {
  size_t i = base::size(kBucketBounds) - 1;
  while (pdf < kBucketBounds[i])
    i--;
  return i;
}

}  // namespace

using SlidingWindowHistogram = DroppedFrameCounter::SlidingWindowHistogram;

void SlidingWindowHistogram::AddPercentDroppedFrame(
    double percent_dropped_frame,
    size_t count) {
  DCHECK_GE(percent_dropped_frame, 0.0);
  DCHECK_GE(100.0, percent_dropped_frame);
  histogram_bins_[static_cast<int>(std::round(percent_dropped_frame))] += count;
  smoothness_buckets_[DecideSmoothnessBucket(percent_dropped_frame)] += count;
  total_count_ += count;
}

uint32_t SlidingWindowHistogram::GetPercentDroppedFramePercentile(
    double percentile) const {
  if (total_count_ == 0)
    return 0;
  DCHECK_GE(percentile, 0.0);
  DCHECK_GE(1.0, percentile);
  int current_index = 100;  // Last bin in historgam
  uint32_t skipped_counter = histogram_bins_[current_index];  // Last bin values
  double samples_to_skip = ((1 - percentile) * total_count_);
  // We expect this method to calculate higher end percentiles such 95 and as a
  // result we count from the last bin to find the correct bin.
  while (skipped_counter < samples_to_skip && current_index > 0) {
    current_index--;
    skipped_counter += histogram_bins_[current_index];
  }
  return current_index;
}

std::vector<double> SlidingWindowHistogram::GetPercentDroppedFrameBuckets()
    const {
  if (total_count_ == 0)
    return std::vector<double>(base::size(kBucketBounds), 0);
  std::vector<double> buckets(base::size(kBucketBounds));
  for (size_t i = 0; i < base::size(kBucketBounds); ++i) {
    buckets[i] =
        static_cast<double>(smoothness_buckets_[i]) * 100 / total_count_;
  }
  return buckets;
}

void SlidingWindowHistogram::Clear() {
  std::fill(std::begin(histogram_bins_), std::end(histogram_bins_), 0);
  std::fill(std::begin(smoothness_buckets_), std::end(smoothness_buckets_), 0);
  total_count_ = 0;
}

std::ostream& SlidingWindowHistogram::Dump(std::ostream& stream) const {
  for (size_t i = 0; i < base::size(histogram_bins_); ++i) {
    stream << i << ": " << histogram_bins_[i] << std::endl;
  }
  return stream << "Total: " << total_count_;
}

std::ostream& operator<<(
    std::ostream& stream,
    const DroppedFrameCounter::SlidingWindowHistogram& histogram) {
  return histogram.Dump(stream);
}

DroppedFrameCounter::DroppedFrameCounter()
    : frame_sorter_(base::BindRepeating(&DroppedFrameCounter::NotifyFrameResult,
                                        base::Unretained(this))) {}
DroppedFrameCounter::~DroppedFrameCounter() = default;

uint32_t DroppedFrameCounter::GetAverageThroughput() const {
  size_t good_frames = 0;
  for (auto it = --end(); it; --it) {
    if (**it == kFrameStateComplete)
      ++good_frames;
  }
  double throughput = 100. * good_frames / ring_buffer_.BufferSize();
  return static_cast<uint32_t>(throughput);
}

void DroppedFrameCounter::AddGoodFrame() {
  ring_buffer_.SaveToBuffer(kFrameStateComplete);
  ++total_frames_;
}

void DroppedFrameCounter::AddPartialFrame() {
  ring_buffer_.SaveToBuffer(kFrameStatePartial);
  ++total_frames_;
  ++total_partial_;
}

void DroppedFrameCounter::AddDroppedFrame() {
  ring_buffer_.SaveToBuffer(kFrameStateDropped);
  ++total_frames_;
  ++total_dropped_;
}

void DroppedFrameCounter::ResetPendingFrames(base::TimeTicks timestamp) {
  // Start with flushing the frames in frame_sorter ignoring the currently
  // pending frames (In other words calling NotifyFrameResult and update
  // smoothness metrics tracked for all frames that have received their ack).
  frame_sorter_.Reset();

  // Before resetting the pending frames, update the measurements for the
  // sliding windows.
  if (!latest_sliding_window_start_.is_null()) {
    const auto report_until = timestamp - kSlidingWindowInterval;
    // Report the sliding window metrics for frames that have already been
    // completed (and some of which may have been dropped).
    while (!sliding_window_.empty()) {
      const auto& args = sliding_window_.front().first;
      if (args.frame_time > report_until)
        break;
      PopSlidingWindow();
    }
    if (sliding_window_.empty()) {
      DCHECK_EQ(dropped_frame_count_in_window_, 0u);
    }

    // Report no dropped frames for the sliding windows spanning the rest of the
    // time.
    if (latest_sliding_window_start_ < report_until) {
      const auto difference = report_until - latest_sliding_window_start_;
      const size_t count =
          std::ceil(difference / latest_sliding_window_interval_);
      if (count > 0)
        sliding_window_histogram_.AddPercentDroppedFrame(0., count);
    }
  }

  dropped_frame_count_in_window_ = 0;
  sliding_window_ = {};
  latest_sliding_window_start_ = {};
  latest_sliding_window_interval_ = {};
}

void DroppedFrameCounter::EnableReporForUI() {
  report_for_ui_ = true;
}

void DroppedFrameCounter::OnBeginFrame(const viz::BeginFrameArgs& args,
                                       bool is_scroll_active) {
  // Remember when scrolling starts/ends. Do this even if fcp has not happened
  // yet.
  if (!is_scroll_active) {
    scroll_start_.reset();
  } else if (!scroll_start_.has_value()) {
    ScrollStartInfo info = {args.frame_time, args.frame_id};
    scroll_start_ = info;
  }

  if (fcp_received_) {
    frame_sorter_.AddNewFrame(args);
    if (is_scroll_active) {
      DCHECK(scroll_start_.has_value());
      scroll_start_per_frame_[args.frame_id] = *scroll_start_;
    }
  }
}

void DroppedFrameCounter::OnEndFrame(const viz::BeginFrameArgs& args,
                                     bool is_dropped) {
  if (!args.interval.is_zero())
    total_frames_in_window_ = kSlidingWindowInterval / args.interval;

  // Don't measure smoothness for frames that start before FCP is received, or
  // that have already been reported as dropped.
  if (is_dropped && fcp_received_ && args.frame_time >= time_fcp_received_ &&
      !frame_sorter_.IsFrameDropped(args.frame_id)) {
    ++total_smoothness_dropped_;

    if (report_for_ui_)
      ReportFramesForUI();
    else
      ReportFrames();
  }
  auto iter = scroll_start_per_frame_.find(args.frame_id);
  if (iter != scroll_start_per_frame_.end()) {
    ScrollStartInfo& scroll_start = iter->second;
    if (args.frame_id.source_id == scroll_start.frame_id.source_id) {
      UMA_HISTOGRAM_CUSTOM_TIMES(
          "Graphics.Smoothness.Diagnostic.DroppedFrameAfterScrollStart.Time",
          (args.frame_time - scroll_start.timestamp), base::Milliseconds(1),
          base::Seconds(4), 50);
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Graphics.Smoothness.Diagnostic.DroppedFrameAfterScrollStart.Frames",
          (args.frame_id.sequence_number -
           scroll_start.frame_id.sequence_number),
          1, 250, 50);
    }
    scroll_start_per_frame_.erase(iter);
  }

  if (fcp_received_)
    frame_sorter_.AddFrameResult(args, is_dropped);
}

void DroppedFrameCounter::ReportFrames() {
  DCHECK(!report_for_ui_);

  const auto total_frames =
      total_counter_->ComputeTotalVisibleFrames(base::TimeTicks::Now());
  TRACE_EVENT2("cc,benchmark", "SmoothnessDroppedFrame", "total", total_frames,
               "smoothness", total_smoothness_dropped_);
  if (sliding_window_max_percent_dropped_ !=
      last_reported_metrics_.max_window) {
    UMA_HISTOGRAM_PERCENTAGE(
        "Graphics.Smoothness.MaxPercentDroppedFrames_1sWindow",
        sliding_window_max_percent_dropped_);
    last_reported_metrics_.max_window = sliding_window_max_percent_dropped_;
  }

  uint32_t sliding_window_95pct_percent_dropped =
      SlidingWindow95PercentilePercentDropped();
  if (sliding_window_95pct_percent_dropped !=
      last_reported_metrics_.p95_window) {
    UMA_HISTOGRAM_PERCENTAGE(
        "Graphics.Smoothness.95pctPercentDroppedFrames_1sWindow",
        sliding_window_95pct_percent_dropped);
    last_reported_metrics_.p95_window = sliding_window_95pct_percent_dropped;
  }

  DCHECK_LE(
      sliding_window_95pct_percent_dropped,
      static_cast<uint32_t>(std::round(sliding_window_max_percent_dropped_)));

  // Emit trace event with most recent smoothness calculation. This matches
  // the smoothness metrics displayed on HeadsUpDisplay.
  TRACE_EVENT2("cc,benchmark", "SmoothnessDroppedFrame::MostRecentCalculation",
               "worst_smoothness", sliding_window_max_percent_dropped_,
               "95_percentile_smoothness",
               sliding_window_95pct_percent_dropped);

  if (ukm_smoothness_data_ && total_frames > 0) {
    UkmSmoothnessData smoothness_data;
    smoothness_data.avg_smoothness =
        static_cast<double>(total_smoothness_dropped_) * 100 / total_frames;
    smoothness_data.worst_smoothness = sliding_window_max_percent_dropped_;
    smoothness_data.percentile_95 = sliding_window_95pct_percent_dropped;

    std::vector<double> sliding_window_buckets =
        sliding_window_histogram_.GetPercentDroppedFrameBuckets();
    DCHECK_EQ(sliding_window_buckets.size(),
              base::size(smoothness_data.buckets));
    std::copy(sliding_window_buckets.begin(), sliding_window_buckets.end(),
              smoothness_data.buckets);

    if (sliding_window_max_percent_dropped_After_1_sec_.has_value())
      smoothness_data.worst_smoothness_after1sec =
          sliding_window_max_percent_dropped_After_1_sec_.value();
    if (sliding_window_max_percent_dropped_After_2_sec_.has_value())
      smoothness_data.worst_smoothness_after2sec =
          sliding_window_max_percent_dropped_After_2_sec_.value();
    if (sliding_window_max_percent_dropped_After_5_sec_.has_value())
      smoothness_data.worst_smoothness_after5sec =
          sliding_window_max_percent_dropped_After_5_sec_.value();
    smoothness_data.time_max_delta = time_max_delta_;
    ukm_smoothness_data_->Write(smoothness_data);
  }
}

void DroppedFrameCounter::ReportFramesForUI() {
  DCHECK(report_for_ui_);
#if BUILDFLAG(IS_CHROMEOS_ASH)
  UMA_HISTOGRAM_PERCENTAGE(
      "Ash.Smoothness.MaxPercentDroppedFrames_1sWindow.Uniform",
      sliding_window_max_percent_dropped_);

  if (sliding_window_max_percent_dropped_ !=
      last_reported_metrics_.max_window) {
    UMA_HISTOGRAM_PERCENTAGE("Ash.Smoothness.MaxPercentDroppedFrames_1sWindow",
                             sliding_window_max_percent_dropped_);
    last_reported_metrics_.max_window = sliding_window_max_percent_dropped_;
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
}

double DroppedFrameCounter::GetMostRecentAverageSmoothness() const {
  if (ukm_smoothness_data_)
    return ukm_smoothness_data_->data.avg_smoothness;

  return -1.f;
}

double DroppedFrameCounter::GetMostRecent95PercentileSmoothness() const {
  if (ukm_smoothness_data_)
    return ukm_smoothness_data_->data.percentile_95;

  return -1.f;
}

void DroppedFrameCounter::SetUkmSmoothnessDestination(
    UkmSmoothnessDataShared* smoothness_data) {
  ukm_smoothness_data_ = smoothness_data;
}

void DroppedFrameCounter::Reset() {
  frame_sorter_.Reset();
  total_frames_ = 0;
  total_partial_ = 0;
  total_dropped_ = 0;
  total_smoothness_dropped_ = 0;
  sliding_window_max_percent_dropped_ = 0;
  sliding_window_max_percent_dropped_After_1_sec_.reset();
  sliding_window_max_percent_dropped_After_2_sec_.reset();
  sliding_window_max_percent_dropped_After_5_sec_.reset();
  dropped_frame_count_in_window_ = 0;
  fcp_received_ = false;
  sliding_window_ = {};
  latest_sliding_window_start_ = {};
  sliding_window_histogram_.Clear();
  ring_buffer_.Clear();
  time_max_delta_ = {};
  last_reported_metrics_ = {};
}

base::TimeDelta DroppedFrameCounter::ComputeCurrentWindowSize() const {
  if (sliding_window_.empty())
    return {};
  return sliding_window_.back().first.frame_time +
         sliding_window_.back().first.interval -
         sliding_window_.front().first.frame_time;
}

void DroppedFrameCounter::NotifyFrameResult(const viz::BeginFrameArgs& args,
                                            bool is_dropped) {
  // Entirely disregard the frames with interval larger than the window --
  // these are violating the assumptions in the below code and should
  // only occur with external frame control, where dropped frame stats
  // are not relevant.
  if (args.interval >= kSlidingWindowInterval)
    return;

  sliding_window_.push({args, is_dropped});

  if (is_dropped)
    ++dropped_frame_count_in_window_;
  if (ComputeCurrentWindowSize() < kSlidingWindowInterval)
    return;

  DCHECK_GE(dropped_frame_count_in_window_, 0u);
  DCHECK_GE(sliding_window_.size(), dropped_frame_count_in_window_);

  while (ComputeCurrentWindowSize() > kSlidingWindowInterval) {
    PopSlidingWindow();
  }
  DCHECK(!sliding_window_.empty());
}

void DroppedFrameCounter::PopSlidingWindow() {
  const auto removed_args = sliding_window_.front().first;
  const auto removed_was_dropped = sliding_window_.front().second;
  if (removed_was_dropped) {
    DCHECK_GT(dropped_frame_count_in_window_, 0u);
    --dropped_frame_count_in_window_;
  }
  sliding_window_.pop();
  if (sliding_window_.empty())
    return;

  // Don't count the newest element if it is outside the current window.
  const auto& newest_args = sliding_window_.back().first;
  const auto newest_was_dropped = sliding_window_.back().second;
  auto dropped = dropped_frame_count_in_window_;
  if (ComputeCurrentWindowSize() > kSlidingWindowInterval && newest_was_dropped)
    --dropped;

  // If two consecutive 'completed' frames are far apart from each other (in
  // time), then report the 'dropped frame count' for the sliding window(s) in
  // between. Note that the window-size still needs to be at least
  // kSlidingWindowInterval.
  const auto max_sliding_window_start =
      newest_args.frame_time - kSlidingWindowInterval;
  const auto max_difference = newest_args.interval * 1.5;
  const auto& remaining_oldest_args = sliding_window_.front().first;
  const auto last_timestamp =
      std::min(remaining_oldest_args.frame_time, max_sliding_window_start);
  const auto difference = last_timestamp - removed_args.frame_time;
  const size_t count = difference > max_difference
                           ? std::ceil(difference / newest_args.interval)
                           : 1;
  double percent_dropped_frame =
      std::min((dropped * 100.0) / total_frames_in_window_, 100.0);
  sliding_window_histogram_.AddPercentDroppedFrame(percent_dropped_frame,
                                                   count);

  if (percent_dropped_frame > sliding_window_max_percent_dropped_) {
    time_max_delta_ = newest_args.frame_time - time_fcp_received_;
    sliding_window_max_percent_dropped_ = percent_dropped_frame;
  }

  latest_sliding_window_start_ = last_timestamp;
  latest_sliding_window_interval_ = remaining_oldest_args.interval;

  UpdateMaxPercentDroppedFrame(percent_dropped_frame);
}

void DroppedFrameCounter::UpdateMaxPercentDroppedFrame(
    double percent_dropped_frame) {
  if (!fcp_received_)
    return;

  const auto fcp_time_delta = latest_sliding_window_start_ - time_fcp_received_;

  if (fcp_time_delta > base::Seconds(1))
    sliding_window_max_percent_dropped_After_1_sec_ =
        std::max(sliding_window_max_percent_dropped_After_1_sec_.value_or(0.0),
                 percent_dropped_frame);
  if (fcp_time_delta > base::Seconds(2))
    sliding_window_max_percent_dropped_After_2_sec_ =
        std::max(sliding_window_max_percent_dropped_After_2_sec_.value_or(0.0),
                 percent_dropped_frame);
  if (fcp_time_delta > base::Seconds(5))
    sliding_window_max_percent_dropped_After_5_sec_ =
        std::max(sliding_window_max_percent_dropped_After_5_sec_.value_or(0.0),
                 percent_dropped_frame);
}

void DroppedFrameCounter::OnFcpReceived() {
  DCHECK(!fcp_received_);
  fcp_received_ = true;
  time_fcp_received_ = base::TimeTicks::Now();
}

}  // namespace cc
