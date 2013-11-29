#include <sys/time.h>
#include <cuda_runtime.h>

#include "utils.h"

#define RED 2

#define CUCHECK(ans) { gpuAssert((ans), __FILE__, __LINE__); }
inline void gpuAssert(cudaError_t code, char *file, int line, bool abort=true)
{
   if (code != cudaSuccess) 
   {
      fprintf(stderr,"GPUassert: %s %s %d\n", cudaGetErrorString(code), file, line);
      if (abort) exit(code);
   }
}

__device__ unsigned char clamp(float value, int min, int max)
{
	if(value < min)
		return 0;
	else if(value > max)
		return max;
	else
		return value;
}

__global__ void RGBToYCBCR(const unsigned char *pixels, float *ycbcrimg, unsigned char *yimg, int width, int height)
{
	unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
	unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
	
	if(x < width && y < height)
	{
		/*
		const float KB = 0.0593;
		const float KR = 0.2627;
		const float KG = 1 - KB - KR;
		
		unsigned char R = pixels[(y * width + x) * 3 + 0];
		unsigned char G = pixels[(y * width + x) * 3 + 1];
		unsigned char B = pixels[(y * width + x) * 3 + 2]; 
		
		float Y = KR * R + (1 - KR - KB) * G + KB * B;
		float CB = (B - Y) / (1 + KR + KG - KB);
		float CR = (R - Y) / (1 - KR + KG + KB);

		yimg[y * width + x] = clamp(Y, 0, 255);

		ycbcrimg[(y * width + x)*3 + 0] = Y;
		ycbcrimg[(y * width + x)*3 + 1] = CB;
		ycbcrimg[(y * width + x)*3 + 2] = CR;
		*/
		
		unsigned char R = pixels[(y * width + x) * 3 + 0];
		unsigned char G = pixels[(y * width + x) * 3 + 1];
		unsigned char B = pixels[(y * width + x) * 3 + 2]; 
		
		float Y = 0.2627f * R + 0.678f * G + 0.0593f * B;
		float CB = (B - Y) / 1.8814f;
		float CR = (R - Y) / 1.4746f;

		yimg[y * width + x] = clamp(Y, 0, 255);
		ycbcrimg[(y * width + x)*3 + 0] = Y;
		ycbcrimg[(y * width + x)*3 + 1] = CB;
		ycbcrimg[(y * width + x)*3 + 2] = CR;
	}
}

__global__ void YCBCRToRGB(const unsigned char *origin, unsigned char *yimg, float *lut, unsigned char *pixels, int width, int height)
{
	unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
	unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
	
	if(x < width && y < height)
	{
		/*
		const float KB = 0.0593;
		const float KR = 0.2627;
		const float KG = 1 - KB - KR;
		
		float Y = lut[yimg[y * width + x]];
		float CB = ycbcrimg[(y * width + x) * 3 + 1];
		float CR = ycbcrimg[(y * width + x) * 3 + 2];
		
		unsigned char R = clamp((Y + CR * (1 - KR + KG + KB)), 0, 255);
		unsigned char B = clamp((Y + CB * (1 + KR + KG - KB)), 0, 255);
		unsigned char G = clamp((Y - KR * R - KB * B) / (1 - KB - KR), 0, 255);
		
		yimg[y * width + x] = Y;
		pixels[(y * width + x) * 3 + 0] = R;
		pixels[(y * width + x) * 3 + 1] = G;
		pixels[(y * width + x) * 3 + 2] = B;
		*/
		unsigned char R = origin[(y * width + x) * 3 + 0];
		unsigned char G = origin[(y * width + x) * 3 + 1];
		unsigned char B = origin[(y * width + x) * 3 + 2]; 
		
		float Y = 0.2627f * R + 0.678f * G + 0.0593f * B;
		float CB = (B - Y) / 1.8814f;
		float CR = (R - Y) / 1.4746f;
		
		Y = lut[yimg[y * width + x]];
		
		R = clamp(Y + CR * 1.4746f, 0, 255);
		B = clamp(Y + CB * 1.8814f, 0, 255);
		G = clamp((Y - 0.2627f * R - 0.0593f * B) / 0.678, 0, 255);
		
		pixels[(y * width + x) * 3 + 0] = R;
		pixels[(y * width + x) * 3 + 1] = G;
		pixels[(y * width + x) * 3 + 2] = B;
	}
}

__global__ void histogram(const unsigned char *pixels, unsigned char *yimg, unsigned int *yhist, int width, int height)
{
	unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
	unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
	
	if(x < width && y < height)
	{	
		unsigned char R = pixels[(y * width + x) * 3 + 0];
		unsigned char G = pixels[(y * width + x) * 3 + 1];
		unsigned char B = pixels[(y * width + x) * 3 + 2]; 
		
		float Y = 0.2627f * R + 0.678f * G + 0.0593f * B;
		
		unsigned char data = clamp(Y, 0, 255);
		yimg[y * width + x] = data;

		atomicAdd(&yhist[((y * width + x) % RED) * 256 + data], 1);
	}
}

__global__ void reduction(unsigned int *yhist, int threads, int width, int height, int level)
{
	unsigned int tid = (blockIdx.x * blockDim.x + threadIdx.x) * level;
	
	if(tid < threads && tid % (level * 2) == 0 && tid + level < threads)
	{		
		for(int i=0; i<256; i++)
		{
			(yhist + tid * 256)[i] += (yhist + (tid + level) * 256)[i];
		}
	}
}

int main(int argc, char **argv)
{
	struct timeval start, last, now, computation;
	
	gettimeofday(&start, 0);
	
	if(argc < 2)
	{
		printf("usage: %s image\n", argv[0]);
		return 0;
	}
	
	ilInit();
	ilEnable(IL_ORIGIN_SET);
	ilEnable(IL_FILE_OVERWRITE);
	
	ILboolean result = ilLoadImage(argv[1]);

	if(!result)
	{
		ILenum err = ilGetError() ;
		printf("Failed to load %s\n", argv[1]);
		printf("Error: %s\n", ilGetString(err));
	}

	ilConvertImage(IL_RGB, IL_UNSIGNED_BYTE);
	ilOriginFunc(IL_ORIGIN_UPPER_LEFT);
		
	ILuint width = ilGetInteger(IL_IMAGE_WIDTH);
	ILuint height = ilGetInteger(IL_IMAGE_HEIGHT);
	
	unsigned int size = width * height;
	
	ILubyte * pixels = ilGetData();
	
	gettimeofday(&now, 0);
	computation = last = now;
	
	printf("Image (%d * %d) loaded in %f\n", width, height, get_time(start, now));
	
	unsigned char *eqpixels = (unsigned char*)malloc(size * 3 * sizeof(unsigned char));
	
	printf("Size: %d * %d\n", width, height);
	
	///=================================================================
	unsigned char *d_pixels = 0, *d_eqpixels = 0;
	unsigned char *d_yimg = 0;
	unsigned int *d_yhist = 0;
	float *d_lut = 0;
	
	CUCHECK(cudaMalloc((void**)&d_pixels, size * 3 * sizeof(unsigned char)));
	CUCHECK(cudaMalloc((void**)&d_yimg, size * sizeof(unsigned char)));
	CUCHECK(cudaMalloc((void**)&d_yhist, 256 * RED * sizeof(unsigned int)));
	CUCHECK(cudaMalloc((void**)&d_lut, 256 * sizeof(float)));
	
	cudaMemcpy(d_pixels, pixels, size * 3 * sizeof(unsigned char), cudaMemcpyHostToDevice);
	cudaMemset(d_yhist, 0, 256 * RED * sizeof(unsigned int));
	
	dim3 blockDim = dim3(16, 16, 1);
	dim3 gridDim = dim3((width + blockDim.x - 1) / blockDim.x, (height + blockDim.y - 1) / blockDim.y, 1);
	histogram<<<gridDim, blockDim, 0>>>(d_pixels, d_yimg, d_yhist, width, height);
	
	cudaThreadSynchronize();
	gettimeofday(&now, 0);
	printf("\tHistogram computed in %f\n", get_time(last, now));
	last = now;
	
	unsigned int *yhist = (unsigned int*)malloc(256 * RED * sizeof(unsigned int));
	
	for(int level=1; level<RED; level*=2)
	{
		blockDim = dim3(8, 1, 1);
		gridDim = dim3((RED / level + blockDim.x - 1) / blockDim.x, 1, 1);
		reduction<<<gridDim, blockDim, 0>>>(d_yhist, RED, width, height, level);
	}
	
	cudaMemcpy(yhist, d_yhist, 256 * RED * sizeof(unsigned int), cudaMemcpyDeviceToHost);
	
	// Equalization
	float ylut[256];
	double ysum = 0;
	for(unsigned int i = 0; i < 256; ++i)
	{
		ysum += (float)yhist[i] / size;
		ylut[i] = ysum * 255;
	}
	
	cudaMemcpy(d_lut, ylut, 256 * sizeof(float), cudaMemcpyHostToDevice);
	free(yhist);
	
	cudaThreadSynchronize();
	gettimeofday(&now, 0);
	printf("\tEqualization computed in %f\n", get_time(last, now));
	last = now;
	
	CUCHECK(cudaMalloc((void**)&d_eqpixels, size * 3 * sizeof(unsigned char)));
	
	// YCBCR -> RGB
	blockDim = dim3(16, 16, 1);
	gridDim = dim3((width + blockDim.x - 1) / blockDim.x, (height + blockDim.y - 1) / blockDim.y, 1);
	YCBCRToRGB<<<gridDim, blockDim, 0>>>(d_pixels, d_yimg, d_lut, d_eqpixels, width, height);

	cudaMemcpy(eqpixels, d_eqpixels, size * 3 * sizeof(unsigned char), cudaMemcpyDeviceToHost);
	
	cudaFree(d_pixels);
	cudaFree(d_eqpixels);
	cudaFree(d_yimg);
	cudaFree(d_yhist);
	cudaFree(d_lut);
	
	cudaThreadSynchronize();
	gettimeofday(&now, 0);
	printf("\tYCBCR -> RGb in %f\n", get_time(last, now));
	printf("Equalization computed in %f\n", get_time(computation, now));
	last = now;
	
	///=================================================================
	
	// Save images
	save_image("histo.jpg", eqpixels, width, height);
	free(eqpixels);
	
	gettimeofday(&now, 0);
	printf("Result saved in %f\n", get_time(last, now));
	printf("Total time %f\n", get_time(start, now));

	return 0;
}
