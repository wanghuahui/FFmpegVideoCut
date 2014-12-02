// ffmpeghelper.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "ffmpeghelper.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")

#ifdef __cplusplus
}
#endif

#define SPLIT_DURATION  10    // seconds
#define START_TIME      0

static int64_t videoTime = 0;
static int64_t audioTime = 0;  // 每段视音频结束时间

// 初始化，注册编解码器
void init()
{
	av_register_all();
}

AVFormatContext* open_input_file(const char* filename)
{
	AVFormatContext *ifmt_ctx = NULL;
	int ret = -1;

	//输入（Input）  
	if ((ret = avformat_open_input(&ifmt_ctx, filename, 0, 0)) < 0) {  
		printf( "Could not open input file.\n");  
		return NULL;  
	}  
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {  
		printf( "Failed to retrieve input stream information.\n");  
		return NULL;  
	}  
	// 输出视频信息
	av_dump_format(ifmt_ctx, 0, filename, 0); 

	return ifmt_ctx;
}

void close_input_file(AVFormatContext *ifmt_ctx)
{
	if (ifmt_ctx)
		avformat_close_input(&ifmt_ctx);  
}

int split_function(AVFormatContext* ifmt_ctx, char* outfile)
{
	if (!ifmt_ctx)
		return -1;
	AVFormatContext *ofmt_ctx = NULL;
	AVOutputFormat *ofmt = NULL;
	AVPacket pkt;
	int i, ret;
	int vstream, astream;
	vstream = astream = -1;

	// 输出
	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outfile);
	if (!ofmt_ctx) {
		printf("Could not create output context.\n");
		return -1;
	}
	ofmt = ofmt_ctx->oformat;
	for (i=0; i<ifmt_ctx->nb_streams; i++)
	{
		if (ifmt_ctx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
			vstream = i;
		else if (ifmt_ctx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO)
			astream = i;

		//根据输入流创建输出流（Create output AVStream according to input AVStream）  
		AVStream *in_stream = ifmt_ctx->streams[i];  
		AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);  
		if (!out_stream) {  
			printf( "Failed allocating output stream\n");  
			return -1;  
		}  
		//复制AVCodecContext的设置（Copy the settings of AVCodecContext）  
		ret = avcodec_copy_context(out_stream->codec, in_stream->codec);  
		if (ret < 0) {  
			printf( "Failed to copy context from input to output stream codec context\n");  
			return -1;  
		}  
		out_stream->codec->codec_tag = 0;  
		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)  
			out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;  
	}
	// 是否找到视频流
	if (vstream == -1)
		return -1;
	//输出一下格式------------------  
	av_dump_format(ofmt_ctx, 0, outfile, 1);  
	//打开输出文件（Open output file）  
	if (!(ofmt->flags & AVFMT_NOFILE)) {  
		ret = avio_open(&ofmt_ctx->pb, outfile, AVIO_FLAG_WRITE);  
		if (ret < 0) {  
			printf( "Could not open output file '%s'\n", outfile);  
			return -1;  
		}  
	}

	avformat_write_header(ofmt_ctx, NULL);

	AVRational bq = {1, AV_TIME_BASE};
	int64_t stime, etime;
	stime = START_TIME * AV_TIME_BASE;
	etime = SPLIT_DURATION * AV_TIME_BASE;
	int64_t ist = av_rescale_q(ifmt_ctx->start_time, bq, ifmt_ctx->streams[vstream]->time_base);
	stime = av_rescale_q(stime, bq, ifmt_ctx->streams[vstream]->time_base);
	stime += ist;  // 视频自身开始偏移
	stime = max(stime, min(videoTime, audioTime));
	etime = av_rescale_q(etime, bq, ifmt_ctx->streams[vstream]->time_base);
	etime += stime;
	av_seek_frame(ifmt_ctx, vstream, stime, AVSEEK_FLAG_ANY);

	bool bfirst = true;
	int frame_index = 0;
	while (1)
	{
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;
		// 找到I帧
		if (bfirst && pkt.stream_index==vstream){
			if (pkt.flags==1){  // I帧
				if (pkt.dts>=videoTime || pkt.pts==0)
					bfirst = false;
				else{
					av_free_packet(&pkt);
					continue;
				}
			}
			else{
				av_free_packet(&pkt);
				continue;
			}
		}	// bfirst
		// 确定末尾I帧
		if (pkt.stream_index == vstream){
			/* 末尾帧如果为I帧且超过结束时间则退出,I帧不写入文件 */
			if (videoTime>etime && pkt.flags==1) 
				break;
			videoTime = pkt.dts;
		}
		// 判断音频写入时间
		if (pkt.stream_index == astream){
			/* audioTime为上个文件结束时的音频时间
			   如果本次开始时间小于上次结束时间，则不写,防止重复写入音频 */
			if (pkt.pts<=audioTime && pkt.pts!=0){
				av_free_packet(&pkt);
				continue;
			}
			audioTime = pkt.pts;
		}

		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);  
		if (ret < 0) {  
			printf( "Error muxing packet\n");  
			break;  
		} 

		if (vstream == pkt.stream_index)
			printf("Write %8d frames to output file\n", ++frame_index);  
		av_free_packet(&pkt);   
	}	// while (1)
	//写文件尾（Write file trailer）  
	av_write_trailer(ofmt_ctx);

	/* close output */  
	if (ofmt_ctx)
	{
		if (!(ofmt->flags & AVFMT_NOFILE))  
			avio_close(ofmt_ctx->pb);  
		avformat_free_context(ofmt_ctx);  
	}
	if (ret < 0 && ret != AVERROR_EOF) {  
		printf( "Error occurred.\n");  
		return -1;  
	}
	if (ret == AVERROR_EOF)  // 如果读到文件末尾，则返回1
		return 0;

	return 1;  
}

DLL_API int SplitVideo(/*char *filename*/)
{
	//if (!filename)
	//{
	//	printf("Invalid input path or output path.\n");
	//	return 0;
	//}
	char *filename = "D:\\VS2010\\myproduct\\ffmpegtest2\\ffmpegtest2\\bin\\Debug\\15m-51ch-001.mkv";

	init();
	AVFormatContext *ifmt_ctx = NULL;
	ifmt_ctx = open_input_file(filename);

	int secs, segments;  // 总的视频秒数，视频分成的段数
	secs = (ifmt_ctx->duration) / 1000000;
	segments = secs / SPLIT_DURATION;
	if (secs%SPLIT_DURATION)  // 有余数
		segments += 1;

	//int startTime, endTime;
	//startTime = 0;
	//endTime = startTime + SPLIT_DURATION;  // 初始值
	int count = 0;
	char outfile[260] = {0};
	int ret = 0;
	while (segments--)
	{
		count ++;
		memset(outfile, 0, sizeof(outfile));
		char *p = strrchr(filename, '.');
		strncpy(outfile, filename, strlen(filename)-strlen(p));
		sprintf(outfile, "%s_%03d%s", outfile, count, p);
		printf("Outfile: %s. Start Splitting...\n", outfile);

		ret = split_function(ifmt_ctx, outfile);
		if (ret<=0)
			break;
	}

	close_input_file(ifmt_ctx);
	return count;
}

DLL_API int test()
{
	char *filename = "D:\\VS2010\\myproduct\\ffmpegtest2\\ffmpegtest2\\bin\\Debug\\15m-51ch-001.mkv";
	//printf("%s\n", filename);

	av_register_all();
	

	printf("this is a test function.\n");
	int a, b;
	a = 1, b = 2;
	
	return a+b;
}