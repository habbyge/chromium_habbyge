# Copyright 2021 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Module for interacting with expectation files."""

import base64
import os
import posixpath

import six

# This script is Python 3-only, but some presubmit stuff still tries to parse
# it in Python 2, and this module does not exist in Python 2.
if six.PY3:
  import urllib.request

import flake_suppressor

from typ import expectations_parser

CHROMIUM_SRC_DIR = flake_suppressor.CHROMIUM_SRC_DIR
RELATIVE_EXPECTATION_FILE_DIRECTORY = os.path.join('content', 'test', 'gpu',
                                                   'gpu_tests',
                                                   'test_expectations')
ABSOLUTE_EXPECTATION_FILE_DIRECTORY = os.path.join(
    CHROMIUM_SRC_DIR, RELATIVE_EXPECTATION_FILE_DIRECTORY)
# For most test suites reported to ResultDB, we can chop off "_integration_test"
# and get the name used for the expectation file. However, there are a few
# special cases, so map those there.
EXPECTATION_FILE_OVERRIDE = {
    'info_collection_test': 'info_collection',
    'trace': 'trace_test',
}
GITILES_URL = 'https://chromium.googlesource.com/chromium/src/+/refs/heads/main'
TEXT_FORMAT_ARG = '?format=TEXT'


def IterateThroughResultsForUser(result_map, group_by_tags):
  """Iterates over |result_map| for the user to provide input.

  For each unique result, user will be able to decide whether to ignore it (do
  nothing), mark as flaky (add RetryOnFailure expectation), or mark as failing
  (add Failure expectation). If the latter two are chosen, they can also
  associate a bug with the new expectation.

  Args:
    result_map: Aggregated query results from results.AggregateResults to
        iterate over.
    group_by_tags: A boolean denoting whether to attempt to group expectations
        by tags or not. If True, expectations will be added after an existing
        expectation whose tags are the largest subset of the produced tags. If
        False, new expectations will be appended to the end of the file.
  """
  typ_tag_ordered_result_map = _ReorderMapByTypTags(result_map)
  for suite, test_map in result_map.items():
    for test, tag_map in test_map.items():
      for typ_tags, build_url_list in tag_map.items():

        print('')
        print('Suite: %s' % suite)
        print('Test: %s' % test)
        print('Configuration:\n    %s' % '\n    '.join(typ_tags))
        print('Failed builds:\n    %s' % '\n    '.join(build_url_list))

        other_failures_for_test = FindFailuresInSameTest(
            result_map, suite, test, typ_tags)
        if other_failures_for_test:
          print('Other failures in same test found on other configurations')
          for (tags, failure_count) in other_failures_for_test:
            print('    %d failures on %s' % (failure_count, ' '.join(tags)))

        other_failures_for_config = FindFailuresInSameConfig(
            typ_tag_ordered_result_map, suite, test, typ_tags)
        if other_failures_for_config:
          print('Other failures on same configuration found in other tests')
          for (name, failure_count) in other_failures_for_config:
            print('    %d failures in %s' % (failure_count, name))

        expected_result, bug = PromptUserForExpectationAction()
        if not expected_result:
          continue

        ModifyFileForResult(suite, test, typ_tags, bug, expected_result,
                            group_by_tags)


def FindFailuresInSameTest(result_map, target_suite, target_test,
                           target_typ_tags):
  """Finds all other failures that occurred in the given test.

  Ignores the failures for the test on the same configuration.

  Args:
    result_map: Aggregated query results from results.AggregateResults.
    target_suite: A string containing the test suite being checked.
    target_test: A string containing the target test case being checked.
    target_typ_tags: A list of strings containing the typ tags that the failure
        took place on.

  Returns:
    A list of tuples (typ_tags, count). |typ_tags| is a list of strings
    defining a configuration the specified test failed on. |count| is how many
    times the test failed on that configuration.
  """
  other_failures = []
  target_typ_tags = tuple(target_typ_tags)
  tag_map = result_map.get(target_suite, {}).get(target_test, {})
  for typ_tags, build_url_list in tag_map.items():
    if typ_tags == target_typ_tags:
      continue
    other_failures.append((typ_tags, len(build_url_list)))
  return other_failures


def FindFailuresInSameConfig(typ_tag_ordered_result_map, target_suite,
                             target_test, target_typ_tags):
  """Finds all other failures that occurred on the given configuration.

  Ignores the failures for the given test on the given configuration.

  Args:
    typ_tag_ordered_result_map: Aggregated query results from
        results.AggregateResults that have been reordered using
        _ReorderMapByTypTags.
    target_suite: A string containing the test suite the original failure was
        found in.
    target_test: A string containing the test case the original failure was
        found in.
    target_typ_tags: A list of strings containing the typ tags defining the
        configuration to find failures for.

  Returns:
    A list of tuples (full_name, count). |full_name| is a string containing a
    test suite and test case concatenated together. |count| is how many times
    |full_name| failed on the configuration specified by |target_typ_tags|.
  """
  target_typ_tags = tuple(target_typ_tags)
  other_failures = []
  suite_map = typ_tag_ordered_result_map.get(target_typ_tags, {})
  for suite, test_map in suite_map.items():
    for test, build_url_list in test_map.items():
      if suite == target_suite and test == target_test:
        continue
      full_name = '%s.%s' % (suite, test)
      other_failures.append((full_name, len(build_url_list)))
  return other_failures


def _ReorderMapByTypTags(result_map):
  """Rearranges|result_map| to use typ tags as the top level keys.

  Args:
    result_map: Aggregated query results from results.AggregateResults

  Returns:
    A dict containing the same contents as |result_map|, but in the following
    format:
    {
      typ_tags (tuple of str): {
        suite (str): {
          test (str): build_url_list (list of str),
        },
      },
    }
  """
  reordered_map = {}
  for suite, test_map in result_map.items():
    for test, tag_map in test_map.items():
      for typ_tags, build_url_list in tag_map.items():
        reordered_map.setdefault(typ_tags,
                                 {}).setdefault(suite,
                                                {})[test] = build_url_list
  return reordered_map


def PromptUserForExpectationAction():
  """Prompts the user on what to do to handle a failure.

  Returns:
    A tuple (expected_result, bug). |expected_result| is a string containing
    the expected result to use for the expectation, e.g. RetryOnFailure. |bug|
    is a string containing the bug to use for the expectation. If the user
    chooses to ignore the failure, both will be None. Otherwise, both are
    filled, although |bug| may be an empty string if no bug is provided.
  """
  prompt = ('How should this failure be handled? (i)gnore/(r)etry on '
            'failure/(f)ailure: ')
  valid_inputs = ['f', 'i', 'r']
  response = input(prompt).lower()
  while response not in valid_inputs:
    print('Invalid input, valid inputs are %s' % (', '.join(valid_inputs)))
    response = input(prompt).lower()

  if response == 'i':
    return (None, None)
  expected_result = 'RetryOnFailure' if response == 'r' else 'Failure'

  prompt = ('What is the bug URL that should be associated with this '
            'expectation? E.g. crbug.com/1234. ')
  response = input(prompt)
  return (expected_result, response)


def ModifyFileForResult(suite, test, typ_tags, bug, expected_result,
                        group_by_tags):
  """Adds an expectation to the appropriate expectation file.

  Args:
    suite: A string containing the suite the failure occurred in.
    test: A string containing the test case the failure occurred in.
    typ_tags: A list of strings containing the typ tags the test produced.
    bug: A string containing the bug to associate with the new expectation.
    expected_result: A string containing the expected result to use for the new
        expectation, e.g. RetryOnFailure.
    group_by_tags: A boolean denoting whether to attempt to group expectations
        by tags or not. If True, expectations will be added after an existing
        expectation whose tags are the largest subset of the produced tags. If
        False, new expectations will be appended to the end of the file.
  """
  expectation_file = GetExpectationFileForSuite(suite, typ_tags)
  bug = '%s ' % bug if bug else bug

  def AppendExpectationToEnd():
    expectation_line = '%s[ %s ] %s [ %s ]\n' % (bug, ' '.join(typ_tags), test,
                                                 expected_result)
    with open(expectation_file, 'a') as outfile:
      outfile.write(expectation_line)

  if group_by_tags:
    insertion_line, best_matching_tags = FindBestInsertionLineForExpectation(
        typ_tags, expectation_file)
    if insertion_line == -1:
      AppendExpectationToEnd()
    else:
      # enumerate starts at 0 but line numbers start at 1.
      insertion_line -= 1
      best_matching_tags = list(best_matching_tags)
      best_matching_tags.sort()
      expectation_line = '%s[ %s ] %s [ %s ]\n' % (
          bug, ' '.join(best_matching_tags), test, expected_result)
      with open(expectation_file) as infile:
        input_contents = infile.read()
      output_contents = ''
      for lineno, line in enumerate(input_contents.splitlines(True)):
        output_contents += line
        if lineno == insertion_line:
          output_contents += expectation_line
      with open(expectation_file, 'w') as outfile:
        outfile.write(output_contents)
  else:
    AppendExpectationToEnd()


def GetExpectationFileForSuite(suite, typ_tags):
  """Finds the correct expectation file for the given suite.

  Args:
    suite: A string containing the test suite to look for.
    typ_tags: A list of strings containing typ tags that were produced by the
        failing test.

  Returns:
    A string containing a filepath to the correct expectation file for |suite|
    and |typ_tags|.
  """
  truncated_suite = suite.replace('_integration_test', '')
  if truncated_suite == 'webgl_conformance':
    if 'webgl-version-2' in typ_tags:
      truncated_suite = 'webgl2_conformance'

  expectation_file = EXPECTATION_FILE_OVERRIDE.get(truncated_suite,
                                                   truncated_suite)
  expectation_file += '_expectations.txt'
  expectation_file = os.path.join(ABSOLUTE_EXPECTATION_FILE_DIRECTORY,
                                  expectation_file)
  return expectation_file


def FindBestInsertionLineForExpectation(typ_tags, expectation_file):
  """Finds the best place to insert an expectation when grouping by tags.

  Args:
    typ_tags: A list of strings containing typ tags that were produced by the
        failing test.
    expectation_file: A string containing a filepath to the expectation file to
        use.

  Returns:
    A tuple (insertion_line, best_matching_tags). |insertion_line| is an int
    specifying the line number to insert the expectation into.
    |best_matching_tags| is a set containing the tags of an existing expectation
    that was found to be the closest match. If no appropriate line is found,
    |insertion_line| is -1 and |best_matching_tags| is empty.
  """
  best_matching_tags = set()
  best_insertion_line = -1
  with open(expectation_file) as f:
    content = f.read()
  list_parser = expectations_parser.TaggedTestListParser(content)
  for e in list_parser.expectations:
    expectation_tags = e.tags
    if not expectation_tags.issubset(typ_tags):
      continue
    if len(expectation_tags) > len(best_matching_tags):
      best_matching_tags = expectation_tags
      best_insertion_line = e.lineno
    elif len(expectation_tags) == len(best_matching_tags):
      if best_insertion_line < e.lineno:
        best_insertion_line = e.lineno
  return best_insertion_line, best_matching_tags


def GetExpectationFilesFromOrigin():
  """Gets expectation file contents from origin/main.

  Returns:
    A dict of expectation file name (str) -> expectation file contents (str)
    that are available on origin/main.
  """
  # Get the path to the expectation file directory in gitiles, i.e. the POSIX
  # path relative to the Chromium src directory.
  origin_dir = RELATIVE_EXPECTATION_FILE_DIRECTORY.replace(os.sep, '/')

  origin_dir_url = posixpath.join(GITILES_URL, origin_dir) + TEXT_FORMAT_ARG
  response = urllib.request.urlopen(origin_dir_url).read()
  # Response is a base64 encoded, newline-separated list of files in the
  # directory in the format: `mode file_type hash name`
  files = []
  decoded_text = base64.b64decode(response).decode('utf-8')
  for line in decoded_text.splitlines():
    files.append(line.split()[-1])

  origin_file_contents = {}
  for f in files:
    origin_filepath = posixpath.join(origin_dir, f)
    origin_filepath_url = posixpath.join(GITILES_URL,
                                         origin_filepath) + TEXT_FORMAT_ARG
    response = urllib.request.urlopen(origin_filepath_url).read()
    decoded_text = base64.b64decode(response).decode('utf-8')
    origin_file_contents[f] = decoded_text

  return origin_file_contents


def GetExpectationFilesFromLocalCheckout():
  """Gets expectaiton file contents from the local checkout.

  Returns:
    A dict of expectation file name (str) -> expectation file contents (str)
    that are available from the local checkout.
  """
  local_file_contents = {}
  for f in os.listdir(ABSOLUTE_EXPECTATION_FILE_DIRECTORY):
    with open(os.path.join(ABSOLUTE_EXPECTATION_FILE_DIRECTORY, f)) as infile:
      local_file_contents[f] = infile.read()
  return local_file_contents


def AssertCheckoutIsUpToDate():
  """Confirms that the local checkout's expectations are up to date."""
  origin_file_contents = GetExpectationFilesFromOrigin()
  local_file_contents = GetExpectationFilesFromLocalCheckout()
  if origin_file_contents != local_file_contents:
    raise RuntimeError(
        'Local Chromium checkout expectations are out of date. Please perform '
        'a `git pull`.')
