// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/history_clusters/core/on_device_clustering_util.h"

#include "base/containers/contains.h"

namespace history_clusters {

void MergeDuplicateVisitIntoCanonicalVisit(
    const history::ClusterVisit& duplicate_visit,
    history::ClusterVisit& canonical_visit) {
  // Upgrade the canonical visit's annotations (i.e. is-bookmarked) with
  // those of the duplicate visits.
  auto& context_annotations =
      canonical_visit.annotated_visit.context_annotations;
  const auto& duplicate_annotations =
      duplicate_visit.annotated_visit.context_annotations;
  context_annotations.is_existing_bookmark |=
      duplicate_annotations.is_existing_bookmark;
  context_annotations.is_existing_part_of_tab_group |=
      duplicate_annotations.is_existing_part_of_tab_group;
  context_annotations.is_new_bookmark |= duplicate_annotations.is_new_bookmark;
  context_annotations.is_placed_in_tab_group |=
      duplicate_annotations.is_placed_in_tab_group;
  context_annotations.is_ntp_custom_link |=
      duplicate_annotations.is_ntp_custom_link;
  context_annotations.omnibox_url_copied |=
      duplicate_annotations.omnibox_url_copied;

  auto& canonical_searches =
      canonical_visit.annotated_visit.content_annotations.related_searches;
  const auto& duplicate_searches =
      duplicate_visit.annotated_visit.content_annotations.related_searches;
  for (const auto& query : duplicate_searches) {
    // This is an n^2 algorithm, but in practice the list of related
    // searches should be on the order of 10 elements long at maximum.
    // If that's not true we should replace this with a set structure.
    if (!base::Contains(canonical_searches, query)) {
      canonical_searches.push_back(query);
    }
  }

  // Roll up the visit duration from the duplicate visit into the canonical
  // visit.
  canonical_visit.annotated_visit.visit_row.visit_duration +=
      duplicate_visit.annotated_visit.visit_row.visit_duration;
  // Only add the foreground duration if it is populated and roll it up.
  base::TimeDelta duplicate_foreground_duration =
      duplicate_visit.annotated_visit.context_annotations
          .total_foreground_duration;
  // Check for > 0 since the default for total_foreground_duration is -1.
  if (duplicate_foreground_duration > base::Seconds(0)) {
    base::TimeDelta canonical_foreground_duration =
        canonical_visit.annotated_visit.context_annotations
            .total_foreground_duration;
    canonical_visit.annotated_visit.context_annotations
        .total_foreground_duration =
        canonical_foreground_duration > base::Seconds(0)
            ? canonical_foreground_duration + duplicate_foreground_duration
            : duplicate_foreground_duration;
  }

  // Add the duplicate visit into the canonical visit's duplicate IDs.
  canonical_visit.duplicate_visit_ids.push_back(
      duplicate_visit.annotated_visit.visit_row.visit_id);
}

base::flat_set<history::VisitID> CalculateAllDuplicateVisitsForCluster(
    const history::Cluster& cluster) {
  base::flat_set<history::VisitID> duplicate_visit_ids;
  for (const auto& visit : cluster.visits) {
    duplicate_visit_ids.insert(visit.duplicate_visit_ids.begin(),
                               visit.duplicate_visit_ids.end());
  }
  return duplicate_visit_ids;
}

}  // namespace history_clusters
