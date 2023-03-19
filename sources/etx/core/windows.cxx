#include <etx/core/windows.hxx>
#include <etx/core/log.hxx>

#if (ETX_PLATFORM_WINDOWS)

#pragma comment(lib, "dbghelp.lib")

namespace etx {

const char* exception_code_to_string(DWORD code) {
#define CASE_TO_STRING(A) \
  case A:                 \
    return #A
  switch (code) {
    CASE_TO_STRING(EXCEPTION_ACCESS_VIOLATION);
    CASE_TO_STRING(EXCEPTION_DATATYPE_MISALIGNMENT);
    CASE_TO_STRING(EXCEPTION_BREAKPOINT);
    CASE_TO_STRING(EXCEPTION_SINGLE_STEP);
    CASE_TO_STRING(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
    CASE_TO_STRING(EXCEPTION_FLT_DENORMAL_OPERAND);
    CASE_TO_STRING(EXCEPTION_FLT_DIVIDE_BY_ZERO);
    CASE_TO_STRING(EXCEPTION_FLT_INEXACT_RESULT);
    CASE_TO_STRING(EXCEPTION_FLT_INVALID_OPERATION);
    CASE_TO_STRING(EXCEPTION_FLT_OVERFLOW);
    CASE_TO_STRING(EXCEPTION_FLT_STACK_CHECK);
    CASE_TO_STRING(EXCEPTION_FLT_UNDERFLOW);
    CASE_TO_STRING(EXCEPTION_INT_DIVIDE_BY_ZERO);
    CASE_TO_STRING(EXCEPTION_INT_OVERFLOW);
    CASE_TO_STRING(EXCEPTION_PRIV_INSTRUCTION);
    CASE_TO_STRING(EXCEPTION_IN_PAGE_ERROR);
    CASE_TO_STRING(EXCEPTION_ILLEGAL_INSTRUCTION);
    CASE_TO_STRING(EXCEPTION_NONCONTINUABLE_EXCEPTION);
    CASE_TO_STRING(EXCEPTION_STACK_OVERFLOW);
    CASE_TO_STRING(EXCEPTION_INVALID_DISPOSITION);
    CASE_TO_STRING(EXCEPTION_GUARD_PAGE);
    CASE_TO_STRING(EXCEPTION_INVALID_HANDLE);
    default:
      return "Unknown exception code";
  }
#undef CASE_TO_STRING
}

LONG WINAPI unhandled_exception_filter(struct _EXCEPTION_POINTERS* info) {
  auto process = GetCurrentProcess();
  SymInitialize(process, nullptr, TRUE);

  void* backtrace[64] = {};
  char symbolInfoData[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
  SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolInfoData);
  symbol->MaxNameLen = MAX_SYM_NAME;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

  DWORD backtraceHash = 0;
  WORD framesCaptured = RtlCaptureStackBackTrace(0, 64u, backtrace, &backtraceHash);

  std::string excCode = exception_code_to_string(info->ExceptionRecord->ExceptionCode);

  printf("Unhandled exception:\n code: %s\n address: 0x%016llX\n", excCode.c_str(), reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress));
  fflush(stdout);

  if (framesCaptured > 0) {
    printf("Backtrace (hash = 0x%08X):\n", backtraceHash);
    fflush(stdout);
    for (unsigned int i = 0; i < framesCaptured; ++i) {
      SymFromAddr(process, reinterpret_cast<DWORD64>(backtrace[i]), 0, symbol);
      printf(" - %s (0x%016llX)\n", symbol->Name, symbol->Address);
      fflush(stdout);
    }
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

void init_platform() {
  SetUnhandledExceptionFilter(unhandled_exception_filter);
  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
}

std::string open_file(const std::vector<std::string>& filters) {
  char name_buffer[MAX_PATH] = {};

  size_t fp = 0;
  char filter_buffer[2048] = {};
  for (const std::string& w : filters) {
    memcpy(filter_buffer + fp, w.data(), w.length());
    fp += 1 + w.length();
  }

  OPENFILENAME of = {};
  of.lStructSize = sizeof(of);
  of.hInstance = GetModuleHandle(nullptr);
  of.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
  of.lpstrFile = name_buffer;
  of.nMaxFile = MAX_PATH;
  of.lpstrFilter = filter_buffer;
  of.nFilterIndex = filters.empty() ? 0 : 1;
  return GetOpenFileNameA(&of) ? of.lpstrFile : "";
}

std::string save_file(const std::vector<std::string>& filters) {
  char name_buffer[MAX_PATH] = {};

  size_t fp = 0;
  char filter_buffer[2048] = {};
  for (const std::string& w : filters) {
    memcpy(filter_buffer + fp, w.data(), w.length());
    fp += 1 + w.length();
  }

  OPENFILENAME of = {};
  of.lStructSize = sizeof(of);
  of.hInstance = GetModuleHandle(nullptr);
  of.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
  of.lpstrFile = name_buffer;
  of.nMaxFile = MAX_PATH;
  of.lpstrFilter = filter_buffer;
  of.nFilterIndex = filters.empty() ? 0 : 1;
  return GetSaveFileName(&of) ? of.lpstrFile : "";
}

float get_cpu_load() {
  auto CalculateCPULoad = [](unsigned long long idleTicks, unsigned long long totalTicks) {
    static unsigned long long _previousTotalTicks = 0;
    static unsigned long long _previousIdleTicks = 0;

    unsigned long long totalTicksSinceLastTime = totalTicks - _previousTotalTicks;
    unsigned long long idleTicksSinceLastTime = idleTicks - _previousIdleTicks;

    float ret = 1.0f - ((totalTicksSinceLastTime > 0) ? ((float)idleTicksSinceLastTime) / totalTicksSinceLastTime : 0);

    _previousTotalTicks = totalTicks;
    _previousIdleTicks = idleTicks;
    return ret;
  };

  auto FileTimeToInt64 = [](const FILETIME& ft) {
    return (((unsigned long long)(ft.dwHighDateTime)) << 32) | ((unsigned long long)ft.dwLowDateTime);
  };

  FILETIME idleTime = {}, kernelTime = {}, userTime = {};
  return GetSystemTimes(&idleTime, &kernelTime, &userTime) ? CalculateCPULoad(FileTimeToInt64(idleTime), FileTimeToInt64(kernelTime) + FileTimeToInt64(userTime)) : -1.0f;
}

inline void log::set_console_color(log::Color clr) {
  auto con = GetStdHandle(STD_OUTPUT_HANDLE);
  switch (clr) {
    case log::Color::White: {
      SetConsoleTextAttribute(con, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
      break;
    }
    case log::Color::Yellow: {
      SetConsoleTextAttribute(con, FOREGROUND_RED | FOREGROUND_GREEN);
      break;
    }
    case log::Color::Red: {
      SetConsoleTextAttribute(con, FOREGROUND_RED);
      break;
    }
    case log::Color::Green: {
      SetConsoleTextAttribute(con, FOREGROUND_GREEN);
      break;
    }
    default:
      SetConsoleTextAttribute(con, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
  }
}


}  // namespace etx

#endif