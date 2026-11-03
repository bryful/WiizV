#pragma once
// These globals are shared with upstream's C translation units.
#ifdef __cplusplus
struct SSettings;
struct SLineMatrixData;
extern "C" {
extern SSettings Settings;
extern unsigned char OpenBus;
extern SLineMatrixData LineMatrixData[];
}
#endif
#ifndef WIIZV_EXPORT
#ifdef _WIN32
#define WIIZV_EXPORT __declspec(dllexport)
#else
#define WIIZV_EXPORT __attribute__((visibility("default")))
#endif
#endif
