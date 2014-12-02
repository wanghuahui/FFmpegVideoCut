// ffmpeghelper.h
// created time: 2014.9.16

#pragma once

#ifdef FFMPEGHELPER_EXPORTS
#define DLL_API extern "C" __declspec(dllexport)
#else
#define DLL_API extern "C" __declspec(dllimport)
#endif

/* Split video
 * filename[in]: video path, e.g. c:\file\1.mkv
 * outpath[in]: output path, e.g. c:\file
 * return: segments of video, <=0 failure
*/
DLL_API int SplitVideo(/*char *filename*/);

DLL_API int test();

