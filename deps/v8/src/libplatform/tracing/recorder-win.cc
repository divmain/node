// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_LIBPLATFORM_TRACING_RECORDER_WIN_H_
#define V8_LIBPLATFORM_TRACING_RECORDER_WIN_H_

#include <regex>

#include "src/libplatform/tracing/etw-metadata.h"
#include "src/libplatform/tracing/recorder.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#endif

#ifndef V8_ETW_GUID
#define V8_ETW_GUID \
  0x57277741, 0x3638, 0x4A4B, 0xBD, 0xBA, 0x0A, 0xC6, 0xE4, 0x5D, 0xA5, 0x6C
#endif

namespace v8 {
namespace platform {
namespace tracing {

TRACELOGGING_DECLARE_PROVIDER(g_v8Provider);

TRACELOGGING_DEFINE_PROVIDER(g_v8Provider, "V8.js", (V8_ETW_GUID));

ScriptMapType* isolate_script_map;

Recorder::Recorder() {
  TraceLoggingRegister(g_v8Provider);
  isolate_script_map = new ScriptMapType();
}

Recorder::~Recorder() {
  if (g_v8Provider) {
    TraceLoggingUnregister(g_v8Provider);
  }
}

bool Recorder::IsEnabled() {
  return TraceLoggingProviderEnabled(g_v8Provider, 0, 0);
}

bool Recorder::IsEnabled(const uint8_t level) {
  return TraceLoggingProviderEnabled(g_v8Provider, level, 0);
}

void Recorder::AddEvent(TraceObject* trace_event) {
  // TODO(sartang@microsoft.com): Figure out how to write the conditional
  // arguments
  wchar_t* wName = new wchar_t[4096];
  MultiByteToWideChar(CP_ACP, 0, trace_event->name(), -1, wName, 4096);

  wchar_t* wCategoryGroupName = new wchar_t[4096];
  MultiByteToWideChar(CP_ACP, 0,
                      TracingController::GetCategoryGroupName(
                          trace_event->category_enabled_flag()),
                      -1, wCategoryGroupName, 4096);

  TraceLoggingWrite(g_v8Provider, "", TraceLoggingValue(wName, "Event Name"),
                    TraceLoggingValue(trace_event->pid(), "pid"),
                    TraceLoggingValue(trace_event->tid(), "tid"),
                    TraceLoggingValue(trace_event->ts(), "ts"),
                    TraceLoggingValue(trace_event->tts(), "tts"),
                    TraceLoggingValue(trace_event->phase(), "phase"),
                    TraceLoggingValue(wCategoryGroupName, "category"),
                    TraceLoggingValue(trace_event->duration(), "dur"),
                    TraceLoggingValue(trace_event->cpu_duration(), "tdur"));
}

void Recorder::CodeEventHandler(const JitCodeEvent* event) {
  if (!IsEnabled()) return;

  if (event->code_type != v8::JitCodeEvent::CodeType::JIT_CODE) return;
  if (event->type != v8::JitCodeEvent::EventType::CODE_ADDED) return;

  int name_len = static_cast<int>(event->name.len);
  // Note: event->name.str is not null terminated.
  std::wstring method_name(name_len + 1, '\0');
  MultiByteToWideChar(
      CP_UTF8, 0, event->name.str, name_len,
      // Const cast needed as building with C++14 (not const in >= C++17)
      const_cast<LPWSTR>(method_name.data()),
      static_cast<int>(method_name.size()));

  void* script_context = static_cast<void*>(event->isolate);
  auto script = event->script;
  if (!script.IsEmpty()) {
    // if the first time seeing this source file, log the SourceLoad event
    auto& script_map = (*isolate_script_map)[script_context];
    if (script_map.find(&script) == script_map.end()) {
      script_map.insert(&script);

      std::wregex script_name_regex(L".* (.*):(?:\\d+).*");
      std::wsmatch m;
      std::wstring script_name = L"[unknown]";
      if (std::regex_match(method_name, m, script_name_regex)) {
        script_name = m[1].str();
      }

      constexpr static auto source_load_event_meta =
          EventMetadata(kSourceLoadEventID, kJScriptRuntimeKeyword);
      constexpr static auto source_load_event_fields = EventFields(
          "SourceLoad", Field("SourceID", TlgInUINT64),
          Field("ScriptContextID", TlgInPOINTER),
          Field("SourceFlags", TlgInUINT32), Field("Url", TlgInUNICODESTRING));
      LogEventData(g_v8Provider, &source_load_event_meta,
                   &source_load_event_fields,
                   (uint64_t)0,  // ScriptID
                   script_context,
                   (uint32_t)0,  // SourceFlags
                   script_name);
    }
  }

  constexpr static auto method_load_event_meta =
      EventMetadata(kMethodLoadEventID, kJScriptRuntimeKeyword);
  constexpr static auto method_load_event_fields = EventFields(
      "MethodLoad", Field("ScriptContextID", TlgInPOINTER),
      Field("MethodStartAddress", TlgInPOINTER),
      Field("MethodSize", TlgInUINT64), Field("MethodID", TlgInUINT32),
      Field("MethodFlags", TlgInUINT16),
      Field("MethodAddressRangeID", TlgInUINT16),
      Field("SourceID", TlgInUINT64), Field("Line", TlgInUINT32),
      Field("Column", TlgInUINT32), Field("MethodName", TlgInUNICODESTRING));

  LogEventData(g_v8Provider, &method_load_event_meta, &method_load_event_fields,
               script_context, event->code_start, (uint64_t)event->code_len,
               (uint32_t)0,               // MethodId
               (uint16_t)0,               // MethodFlags
               (uint16_t)0,               // MethodAddressRangeId
               (uint64_t)0,               // ScriptID
               (uint32_t)0, (uint32_t)0,  // Line & Column
               method_name);
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_RECORDER_WIN_H_
