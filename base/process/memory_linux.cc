// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/memory.h"

#include <stddef.h>

#include <new>

#include "base/allocator/allocator_shim.h"
#include "base/allocator/buildflags.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/process/internal_linux.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"

#if BUILDFLAG(USE_TCMALLOC)
#include "third_party/tcmalloc/chromium/src/config.h"
#include "third_party/tcmalloc/chromium/src/gperftools/tcmalloc.h"
#endif

namespace base {

namespace {

/**
 * @brief 当new分配失败时，调用这个函数：释放保留的地址空间，释放失败的话，std::terminate()终止进程
 */
void ReleaseReservationOrTerminate() {
  if (internal::ReleaseAddressSpaceReservation())
    return;
  TerminateBecauseOutOfMemory(0);
}

}  // namespace

void EnableTerminationOnHeapCorruption() {
  // On Linux, there nothing to do AFAIK.
}

void EnableTerminationOnOutOfMemory() {
  // Set the new-out of memory handler.
  // C++ 标准库函数 std::set_new_handler 用于设置 new 运算符在无法分配足够内存时应调用的处理函数
  std::set_new_handler(&ReleaseReservationOrTerminate);
  // If we're using glibc's allocator, the above functions will override
  // malloc and friends and make them die on out of memory.

#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  allocator::SetCallNewHandlerOnMallocFailure(true);
#elif defined(USE_TCMALLOC)
  // For tcmalloc, we need to tell it to behave like new.
  tc_set_new_mode(1);
#endif
}

// ScopedAllowBlocking() has private constructor and it can only be used in
// friend classes/functions. Declaring a class is easier in this situation to
// avoid adding more dependency to thread_restrictions.h because of the
// parameter used in AdjustOOMScore(). Specifically, ProcessId is a typedef
// and we'll need to include another header file in thread_restrictions.h
// without the class.
// ScopedAllowBlocking() 有private构造函数，它只能在友元类/函数中使用。
// 在这种情况下，声明一个类会更容易，以避免由于在AdjustOOMScore() 中使用的参数而增加对 
// thread_restrictions.h 的更多依赖。
// 具体来说，ProcessId 是一个 typedef，我们需要在 thread_restrictions.h 中包含另一个没有该类的头文件。
class AdjustOOMScoreHelper {
 public:
  AdjustOOMScoreHelper() = delete;
  AdjustOOMScoreHelper(const AdjustOOMScoreHelper&) = delete;
  AdjustOOMScoreHelper& operator=(const AdjustOOMScoreHelper&) = delete;

  static bool AdjustOOMScore(ProcessId process, int score);
};

// static.
bool AdjustOOMScoreHelper::AdjustOOMScore(ProcessId process, int score) {
  if (score < 0 || score > kMaxOomScore)
    return false;

  FilePath oom_path(internal::GetProcPidDir(process));

  // Temporarily allowing blocking since oom paths are pseudo-filesystem paths.
  base::ScopedAllowBlocking allow_blocking;

  // Attempt to write the newer oom_score_adj file first.
  FilePath oom_file = oom_path.AppendASCII("oom_score_adj");
  if (PathExists(oom_file)) {
    std::string score_str = NumberToString(score);
    DVLOG(1) << "Adjusting oom_score_adj of " << process << " to "
             << score_str;
    int score_len = static_cast<int>(score_str.length());
    return (score_len == WriteFile(oom_file, score_str.c_str(), score_len));
  }

  // If the oom_score_adj file doesn't exist, then we write the old
  // style file and translate the oom_adj score to the range 0-15.
  oom_file = oom_path.AppendASCII("oom_adj");
  if (PathExists(oom_file)) {
    // Max score for the old oom_adj range.  Used for conversion of new
    // values to old values.
    const int kMaxOldOomScore = 15;

    int converted_score = score * kMaxOldOomScore / kMaxOomScore;
    std::string score_str = NumberToString(converted_score);
    DVLOG(1) << "Adjusting oom_adj of " << process << " to " << score_str;
    int score_len = static_cast<int>(score_str.length());
    return (score_len == WriteFile(oom_file, score_str.c_str(), score_len));
  }

  return false;
}

// NOTE: This is not the only version of this function in the source:
// the setuid sandbox (in process_util_linux.c, in the sandbox source)
// also has its own C version.
bool AdjustOOMScore(ProcessId process, int score) {
  return AdjustOOMScoreHelper::AdjustOOMScore(process, score);
}

bool UncheckedMalloc(size_t size, void** result) {
#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  *result = allocator::UncheckedAlloc(size);
#elif defined(MEMORY_TOOL_REPLACES_ALLOCATOR) || \
    (!defined(LIBC_GLIBC) && !BUILDFLAG(USE_TCMALLOC))
  *result = malloc(size);
#elif defined(LIBC_GLIBC) && !BUILDFLAG(USE_TCMALLOC)
  *result = __libc_malloc(size);
#elif BUILDFLAG(USE_TCMALLOC)
  *result = tc_malloc_skip_new_handler(size);
#endif
  return *result != nullptr;
}

}  // namespace base
