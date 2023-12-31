// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SERVICES_AUCTION_WORKLET_AUCTION_V8_HELPER_H_
#define CONTENT_SERVICES_AUCTION_WORKLET_AUCTION_V8_HELPER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/memory/ref_counted_delete_on_sequence.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/strings/string_piece.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "content/services/auction_worklet/console.h"
#include "gin/public/isolate_holder.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/devtools/devtools_agent.mojom.h"
#include "url/gurl.h"
#include "v8/include/v8-forward.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-locker.h"
#include "v8/include/v8-persistent-handle.h"

namespace v8 {
class UnboundScript;
}  // namespace v8

namespace v8_inspector {
class V8Inspector;
}  // namespace v8_inspector

namespace auction_worklet {

class AuctionV8DevToolsAgent;
class DebugCommandQueue;

// Helper for Javascript operations. Owns a V8 isolate, and manages operations
// on it. Must be deleted after all V8 objects created using its isolate. It
// facilitates creating objects from JSON and running scripts in isolated
// contexts.
//
// Currently, multiple AuctionV8Helpers can be in use at once, each will have
// its own V8 isolate.  All AuctionV8Helpers are assumed to be created on the
// same thread (V8 startup is done only once per process, and not behind a
// lock).  After creation, all operations on the helper must be done on the
// thread represented by the `v8_runner` argument to Create(); it's the caller's
// responsibility to ensure the methods are invoked there.
class AuctionV8Helper
    : public base::RefCountedDeleteOnSequence<AuctionV8Helper> {
 public:
  // Timeout for script execution.
  static const base::TimeDelta kScriptTimeout;

  // Debugger context group ID asking for no debugging.
  static const int kNoDebugContextGroupId = 0;

  // Helper class to set up v8 scopes to use Isolate. All methods expect a
  // FullIsolateScope to be have been created on the current thread, and a
  // context to be entered.
  class FullIsolateScope {
   public:
    explicit FullIsolateScope(AuctionV8Helper* v8_helper);
    explicit FullIsolateScope(const FullIsolateScope&) = delete;
    FullIsolateScope& operator=(const FullIsolateScope&) = delete;
    ~FullIsolateScope();

   private:
    const v8::Locker locker_;
    const v8::Isolate::Scope isolate_scope_;
    const v8::HandleScope handle_scope_;
  };

  explicit AuctionV8Helper(const AuctionV8Helper&) = delete;
  AuctionV8Helper& operator=(const AuctionV8Helper&) = delete;

  static scoped_refptr<AuctionV8Helper> Create(
      scoped_refptr<base::SingleThreadTaskRunner> v8_runner);
  static scoped_refptr<base::SingleThreadTaskRunner> CreateTaskRunner();

  scoped_refptr<base::SequencedTaskRunner> v8_runner() const {
    return v8_runner_;
  }

  v8::Isolate* isolate() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return isolate_holder_->isolate();
  }

  // Context that can be used for persistent items that can then be used in
  // other contexts - compiling functions, creating objects, etc.
  v8::Local<v8::Context> scratch_context() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return scratch_context_.Get(isolate());
  }

  // Create a v8::Context. The one thing this does that v8::Context::New() does
  // not is remove access the Date object. It also (for now) installs some
  // rudimentary console emulation.
  v8::Local<v8::Context> CreateContext(
      v8::Local<v8::ObjectTemplate> global_template =
          v8::Local<v8::ObjectTemplate>());

  // Creates a v8::String from an ASCII string literal, which should never fail.
  v8::Local<v8::String> CreateStringFromLiteral(const char* ascii_string);

  // Attempts to create a v8::String from a UTF-8 string. Returns empty string
  // if input is not UTF-8.
  v8::MaybeLocal<v8::String> CreateUtf8String(base::StringPiece utf8_string);

  // The passed in JSON must be a valid UTF-8 JSON string.
  v8::MaybeLocal<v8::Value> CreateValueFromJson(v8::Local<v8::Context> context,
                                                base::StringPiece utf8_json);

  // Convenience wrappers around the above Create* methods. Attempt to create
  // the corresponding value type and append it to the passed in argument
  // vector. Useful for assembling arguments to a Javascript function. Return
  // false on failure.
  bool AppendUtf8StringValue(base::StringPiece utf8_string,
                             std::vector<v8::Local<v8::Value>>* args)
      WARN_UNUSED_RESULT;
  bool AppendJsonValue(v8::Local<v8::Context> context,
                       base::StringPiece utf8_json,
                       std::vector<v8::Local<v8::Value>>* args)
      WARN_UNUSED_RESULT;

  // Convenience wrapper that adds the specified value into the provided Object.
  bool InsertValue(base::StringPiece key,
                   v8::Local<v8::Value> value,
                   v8::Local<v8::Object> object) WARN_UNUSED_RESULT;

  // Convenience wrapper that creates an Object by parsing `utf8_json` as JSON
  // and then inserts it into the provided Object.
  bool InsertJsonValue(v8::Local<v8::Context> context,
                       base::StringPiece key,
                       base::StringPiece utf8_json,
                       v8::Local<v8::Object> object) WARN_UNUSED_RESULT;

  // Attempts to convert |value| to JSON and write it to |out|. Returns false on
  // failure.
  bool ExtractJson(v8::Local<v8::Context> context,
                   v8::Local<v8::Value> value,
                   std::string* out);

  // Compiles the provided script. Despite not being bound to a context, there
  // still must be an active context for this method to be invoked. In case of
  // an error sets `error_out`.
  v8::MaybeLocal<v8::UnboundScript> Compile(
      const std::string& src,
      const GURL& src_url,
      int context_group_id,
      absl::optional<std::string>& error_out);

  // Binds a script and runs it in the passed in context, returning the result.
  // Note that the returned value could include references to objects or
  // functions contained within the context, so is likely not safe to use in
  // other contexts without sanitization.
  //
  // If `context_group_id` is not kNoDebugContextGroupId (0), and a debugger
  // connection has been instantiated, will notify debugger of `context`.
  //
  // Assumes passed in context is the active context. Passed in context must be
  // using the Helper's isolate.
  //
  // Running this multiple times in the same context will re-load the entire
  // script file in the context, and then run the script again.
  //
  // In case of an error or console output sets `error_out`.
  v8::MaybeLocal<v8::Value> RunScript(v8::Local<v8::Context> context,
                                      v8::Local<v8::UnboundScript> script,
                                      int context_group_id,
                                      base::StringPiece function_name,
                                      base::span<v8::Local<v8::Value>> args,
                                      std::vector<std::string>& error_out);

  // If any debugging session targeting `context_group_id` has set an active
  // DOM instrumentation breakpoint `name`, asks for v8 to do a debugger pause
  // on the next statement.
  //
  // Expected to be run before a corresponding RunScript.
  void MaybeTriggerInstrumentationBreakpoint(int context_group_id,
                                             const std::string& name);

  void set_script_timeout_for_testing(base::TimeDelta script_timeout);

  // If non-nullptr, this returns a pointer to the of vector representing the
  // debug output lines of the currently running script.  It's nullptr when
  // nothing is running.
  std::vector<std::string>* console_buffer() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return console_buffer_;
  }

  // Returns a string identifying the currently running script for purpose of
  // attributing its debug output in a human-understandable way. Empty if
  // nothing is running.
  const std::string& console_script_name() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return console_script_name_;
  }

  // V8 Debug functionality identifies what to operate on via numeric
  // "context group IDs".

  // Grabs an ID for a particular consumer, and sets the callback to use to
  // resume its execution if it was paused on start.  Since Resume() can be
  // called over Mojo pipes that are unordered with respect to main worklet Mojo
  // pipes, the callback should probably be bound to a WeakPtr.
  //
  // Returned ID will be a positive integer.
  int AllocContextGroupIdAndSetResumeCallback(
      base::OnceClosure resume_callback);

  // Frees up an ID that'll no longer be in use.
  void FreeContextGroupId(int context_group_id);

  // Invokes the registered resume callback for given ID. Does nothing if it
  // was already invoked.
  void Resume(int context_group_id);

  // Overrides what ID will be remembered as last returned to help check the
  // allocation algorithm.
  void SetLastContextGroupIdForTesting(int new_last_id) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    last_context_group_id_ = new_last_id;
  }

  // Calls Resume on all registered context group IDs.
  void ResumeAllForTesting();

  // Establishes a debugger connection, initializing debugging objects if
  // needed, and associating the connection with the given `context_group_id`.
  //
  // The debugger Mojo objects will primarily live on the v8 thread, but
  // `mojo_sequence` will be used for a secondary communication channel in case
  // the v8 thread is blocked. It must be distinct from v8_runner(). Only the
  // value passed in for `mojo_sequence` the first time this method is called
  // will be used.
  void ConnectDevToolsAgent(
      mojo::PendingReceiver<blink::mojom::DevToolsAgent> agent,
      scoped_refptr<base::SequencedTaskRunner> mojo_sequence,
      int context_group_id);

  // Returns the v8 inspector if one has been set. null if ConnectDevToolsAgent
  // (or SetV8InspectorForTesting) hasn't been called.
  v8_inspector::V8Inspector* inspector();

  void SetV8InspectorForTesting(
      std::unique_ptr<v8_inspector::V8Inspector> v8_inspector);

  // Temporarily disables (and re-enables) script timeout for the currently
  // running script. Total time elapsed when not paused will be kept track of.
  //
  // Must be called when within RunScript() only.
  void PauseTimeoutTimer();
  void ResumeTimeoutTimer();

  // Returns the sequence where the timeout timer runs.
  // This may be called on any thread.
  scoped_refptr<base::SequencedTaskRunner> GetTimeoutTimerRunnerForTesting();

  // Helper for formatting script name for debug messages.
  std::string FormatScriptName(v8::Local<v8::UnboundScript> script);

 private:
  friend class base::RefCountedDeleteOnSequence<AuctionV8Helper>;
  friend class base::DeleteHelper<AuctionV8Helper>;
  class ScriptTimeoutHelper;

  // Sets values of console_buffer() and console_script_name() to those
  // passed-in to its constructor for duration of its existence, and clears
  // them afterward.
  class ScopedConsoleTarget {
   public:
    ScopedConsoleTarget(AuctionV8Helper* owner,
                        const std::string& console_script_name,
                        std::vector<std::string>* out);
    ~ScopedConsoleTarget();

   private:
    AuctionV8Helper* owner_;
  };

  explicit AuctionV8Helper(
      scoped_refptr<base::SingleThreadTaskRunner> v8_runner);
  ~AuctionV8Helper();

  void CreateIsolate();

  static std::string FormatExceptionMessage(v8::Local<v8::Context> context,
                                            v8::Local<v8::Message> message);
  static std::string FormatValue(v8::Isolate* isolate,
                                 v8::Local<v8::Value> val);

  scoped_refptr<base::SequencedTaskRunner> v8_runner_;
  scoped_refptr<base::SequencedTaskRunner> timer_task_runner_;

  std::unique_ptr<gin::IsolateHolder> isolate_holder_
      GUARDED_BY_CONTEXT(sequence_checker_);
  Console console_ GUARDED_BY_CONTEXT(sequence_checker_){this};
  v8::Global<v8::Context> scratch_context_
      GUARDED_BY_CONTEXT(sequence_checker_);
  // Script timeout. Can be changed for testing.
  base::TimeDelta script_timeout_ GUARDED_BY_CONTEXT(sequence_checker_) =
      kScriptTimeout;

  // See corresponding getters for description.
  std::vector<std::string>* console_buffer_
      GUARDED_BY_CONTEXT(sequence_checker_) = nullptr;
  std::string console_script_name_ GUARDED_BY_CONTEXT(sequence_checker_);

  ScriptTimeoutHelper* timeout_helper_ GUARDED_BY_CONTEXT(sequence_checker_) =
      nullptr;

  int last_context_group_id_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;

  // This is keyed by group IDs, and is used to keep track of what's valid.
  std::map<int, base::OnceClosure> resume_callbacks_
      GUARDED_BY_CONTEXT(sequence_checker_);

  std::unique_ptr<DebugCommandQueue> debug_command_queue_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Destruction order between `devtools_agent_` and `v8_inspector_` is
  // relevant; see also comment in ~AuctionV8Helper().
  std::unique_ptr<AuctionV8DevToolsAgent> devtools_agent_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<v8_inspector::V8Inspector> v8_inspector_
      GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace auction_worklet

#endif  // CONTENT_SERVICES_AUCTION_WORKLET_AUCTION_V8_HELPER_H_
