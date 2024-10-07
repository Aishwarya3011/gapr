#include <stddef.h>

/*! 标注输入参数，由调用dll的外部程序提供输入 */
#define dll_in
/*! 标注输出参数，由dll为外部提供输出 */
#define dll_out


/*!
 * 读取数据的上下文，结构体在dll中任意定义，可用于管理缓存、线程池、调度等全局信息。
 * 由_new和_free分配和释放。
 */
typedef struct image_loader_context image_loader_context;

/*!
 * 分配上下文。
 * pars为决定数据集的参数，可以是路径、URL或ID等。
 */
dll_out image_loader_context* image_loader_context_new(dll_in const char* pars);
/*!
 * 释放上下文中的资源。
 */
void image_loader_context_free(dll_in image_loader_context* ctx);

/*!
 * 像素类型，先只支持8位和16位整数，其余及错误视为INVALID。
 */
typedef enum image_loader_pixel_type {
	IMAGE_LOADER_PIXEL_TYPE_INVALID,
	IMAGE_LOADER_PIXEL_TYPE_U8,
	IMAGE_LOADER_PIXEL_TYPE_U16,
} image_loader_pixel_type;

/*!
 * 获得数据集像素类型。
 */
dll_out image_loader_pixel_type image_loader_context_get_pixel_type(dll_in image_loader_context* ctx);

/*!
 * 获得数据集分辨率（um/pixel）。
 * resolutions指向连续3个可写的浮点数。
 */
void image_loader_context_get_resolutions(dll_in image_loader_context* ctx, dll_out double* resolutions);

/*!
 * 获得数据集总体的长宽高（pixel，拼接之后规则的立方体）。
 * sizes指向3个可写的整数。
 */
void image_loader_context_get_sizes(dll_in image_loader_context* ctx, dll_out unsigned int* sizes);

/*!
 * 获得数据块的长宽高（pixel），应该是一个适宜一次读取的大小。
 * block_sizes指向3个可写的整数。
 */
void image_loader_context_get_block_sizes(dll_in image_loader_context* ctx, dll_out unsigned int* block_sizes);

/*!
 * 读取在指定位置的数据块，写入buffer。
 *
 * offsets指向3个整数，以距原点的偏移量（pixel）指定要读取数据块的位置，
 * x方向读取范围为[offsets[0], offsets[0]+block_sizes[0])，y、z类似，
 * 上限超出总体范围时只读取范围内部分。
 *
 * 写入内存时，y方向相邻像素写入地址间隔ystride字节，z类似，x方向紧密排列。
 *
 * 如果会因CPU使用限速时，最好能支持多线程调用。
 */
void image_loader_context_load_voxels(dll_in image_loader_context* ctx, dll_in const unsigned int* offsets, dll_out char* buffer, dll_in size_t ystride, dll_in size_t zstride);


/*!
 * 如果有比“逐一读取原始分辨率数据临时生成”更快的获得降采样数据的方法，最好能提供读取降采样数据的接口。
 * 像素类型与原始分辨率数据一致。
 * 降采样数据不分块。
 */
void image_loader_context_get_resolutions_downsample(dll_in image_loader_context* ctx, dll_out double* resolutions);
void image_loader_context_get_sizes_downsample(dll_in image_loader_context* ctx, dll_out unsigned int* sizes);
void image_loader_context_load_voxels_downsample(dll_in image_loader_context* ctx, dll_out char* buffer, dll_in size_t ystride, dll_in size_t zstride);

