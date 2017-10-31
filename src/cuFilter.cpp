#pragma waring(disable:4819)
#include<stdlib.h>
#include<stdio.h>
#include<math.h>
#include<string.h>

#include <helper_gl.h>
#if defined (__APPLE__) || defined(MACOSX)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  #include <GLUT/glut.h>
  #ifndef glutCloseFunc
  #define glutCloseFunc glutWMCloseFunc
  #endif
#else
#include <GL/freeglut.h>
#endif

// CUDA includes and interop headers
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

// CUDA utilities and system includes
#include <helper_functions.h>
#include <helper_cuda.h>      // includes cuda.h and cuda_runtime_api.h
#include <helper_cuda_gl.h>   // includes cuda_runtime_api.h


#define MAX(a,b) ((a > b) ? a : b)

#define USE_SIMPLE_FILTER 0

#define MAX_EPSILON_ERROR 5.0f
#define THRESHOLD  0.15f

// Define the files that are to be save and the reference images for validation
const char *sOriginal[] =
{
    "lena_10.ppm",
    "lena_14.ppm",
    "lena_18.ppm",
    "lena_22.ppm",
    NULL
};

const char *sReference[] =
{
    "ref_10.ppm",
    "ref_14.ppm",
    "ref_18.ppm",
    "ref_22.ppm",
    NULL
};

const char *image_filename = "lena.ppm";
float sigma = 1.0f;
int order = 0;
int nthreads = 64;  // number of threads per block

unsigned int width, height;
unsigned int *h_img = NULL;
unsigned int *d_img = NULL;
unsigned int *d_temp = NULL;

GLuint pbo = 0;     // OpenGL pixel buffer object
GLuint texid = 0;   // texture

StopWatchInterface *timer = 0;

// Auto-Verification Code
const int frameCheckNumber = 4;
int fpsCount = 0;        // FPS count for averaging
int fpsLimit = 1;        // FPS limit for sampling
unsigned int frameCount = 0;

int *pArgc = NULL;
char **pArgv = NULL;

bool runBenchmark = false;

const char *sSDKsample = "CUDA Recursive Gaussian";

extern "C"
void transpose(unsigned int *d_src, unsigned int *d_dest, unsigned int width, int height);

extern "C"
void gaussianFilterRGBA(unsigned int *d_src, unsigned int *d_dest, unsigned int *d_temp, int width, int height, float sigma, int order, int nthreads);

void cleanup();

void computeFPS()
{
    frameCount++;
    fpsCount++;

    if (fpsCount == fpsLimit)
    {
        char fps[256];
        float ifps = 1.f / (sdkGetAverageTimerValue(&timer) / 1000.f);
        sprintf(fps, "%s (sigma=%4.2f): %3.1f fps", sSDKsample, sigma, ifps);

        glutSetWindowTitle(fps);
        fpsCount = 0;

        fpsLimit = ftoi(MAX(ifps, 1.f));
        sdkResetTimer(&timer);
    }
}

// display results using OpenGL
void display()
{
    sdkStartTimer(&timer);

    // execute filter, writing results to pbo
    unsigned int *d_result;
    checkCudaErrors(cudaGLMapBufferObject((void **)&d_result, pbo));
    gaussianFilterRGBA(d_img, d_result, d_temp, width, height, sigma, order, nthreads);
//device result in d_result
    checkCudaErrors(cudaGLUnmapBufferObject(pbo));

    // load texture from pbo
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, pbo);
    glBindTexture(GL_TEXTURE_2D, texid);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

    // display results
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 1);
    glVertex2f(0, 0);
    glTexCoord2f(1, 1);
    glVertex2f(1, 0);
    glTexCoord2f(1, 0);
    glVertex2f(1, 1);
    glTexCoord2f(0, 0);
    glVertex2f(0, 1);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glutSwapBuffers();

    sdkStopTimer(&timer);

    computeFPS();
}

void idle()
{
    glutPostRedisplay();
}

void cleanup()
{
    sdkDeleteTimer(&timer);

    checkCudaErrors(cudaFree(d_img));
    checkCudaErrors(cudaFree(d_temp));

    if (!runBenchmark)
    {
        if (pbo)
        {
            checkCudaErrors(cudaGLUnregisterBufferObject(pbo));
            glDeleteBuffers(1, &pbo);
        }

        if (texid)
        {
            glDeleteTextures(1, &texid);
        }
    }
}

void keyboard(unsigned char key, int x, int y)
{
    switch (key)
    {
        case 27:
            #if defined(__APPLE__) || defined(MACOSX)
                exit(EXIT_SUCCESS);
            #else
                glutDestroyWindow(glutGetWindow());
                return;
            #endif
            break;

        case '=':
            sigma+=0.1f;
            break;

        case '-':
            sigma-=0.1f;

            if (sigma < 0.0)
            {
                sigma = 0.0f;
            }

            break;

        case '+':
            sigma+=1.0f;
            break;

        case '_':
            sigma-=1.0f;

            if (sigma < 0.0)
            {
                sigma = 0.0f;
            }

            break;

        case '0':
            order = 0;
            break;

        case '1':
            order = 1;
            sigma = 0.5f;
            break;

        case '2':
            order = 2;
            sigma = 0.5f;
            break;

        default:
            break;
    }

    printf("sigma = %f\n", sigma);
    glutPostRedisplay();
}

void reshape(int x, int y)
{
    glViewport(0, 0, x, y);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
}

void initCudaBuffers()
{
    unsigned int size = width * height * sizeof(unsigned int);

    // allocate device memory
    checkCudaErrors(cudaMalloc((void **) &d_img, size));
    checkCudaErrors(cudaMalloc((void **) &d_temp, size));

    checkCudaErrors(cudaMemcpy(d_img, h_img, size, cudaMemcpyHostToDevice));

    sdkCreateTimer(&timer);
}


void initGLBuffers()
{
    // create pixel buffer object to store final image
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER_ARB, width*height*sizeof(GLubyte)*4, h_img, GL_STREAM_DRAW_ARB);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    checkCudaErrors(cudaGLRegisterBufferObject(pbo));

    // create texture for display
    glGenTextures(1, &texid);
    glBindTexture(GL_TEXTURE_2D, texid);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void initGL(int *argc, char **argv)
{
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(width, height);
    glutCreateWindow(sSDKsample);
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);

#if defined (__APPLE__) || defined(MACOSX)
    atexit(cleanup);
#else
    glutCloseFunc(cleanup);
#endif

    printf("Press '+' and '-' to change filter width\n");
    printf("0, 1, 2 - change filter order\n");

    if (!isGLVersionSupported(2,0) || !areGLExtensionsSupported("GL_ARB_vertex_buffer_object GL_ARB_pixel_buffer_object"))
    {
        fprintf(stderr, "Required OpenGL extensions missing.");
        exit(EXIT_FAILURE);
    }
}

void
benchmark(int iterations)
{
    // allocate memory for result
    unsigned int *d_result;
    unsigned int size = width * height * sizeof(unsigned int);
    checkCudaErrors(cudaMalloc((void **) &d_result, size));

    // warm-up
    gaussianFilterRGBA(d_img, d_result, d_temp, width, height, sigma, order, nthreads);

    checkCudaErrors(cudaDeviceSynchronize());
    sdkStartTimer(&timer);

    // execute the kernel
    for (int i = 0; i < iterations; i++)
    {
        gaussianFilterRGBA(d_img, d_result, d_temp, width, height, sigma, order, nthreads);
    }

    checkCudaErrors(cudaDeviceSynchronize());
    sdkStopTimer(&timer);

    // check if kernel execution generated an error
    getLastCudaError("Kernel execution failed");

    printf("Processing time: %f (ms)\n", sdkGetTimerValue(&timer));
    printf("%.2f Mpixels/sec\n", (width*height*iterations / (sdkGetTimerValue(&timer) / 1000.0f)) / 1e6);

    checkCudaErrors(cudaFree(d_result));
}

bool
runSingleTest(const char *ref_file, const char *exec_path)
{
    // allocate memory for result
    int nTotalErrors = 0;
    unsigned int *d_result;
    unsigned int size = width * height * sizeof(unsigned int);
    checkCudaErrors(cudaMalloc((void **) &d_result, size));

    // warm-up
    gaussianFilterRGBA(d_img, d_result, d_temp, width, height, sigma, order, nthreads);

    checkCudaErrors(cudaDeviceSynchronize());
    sdkStartTimer(&timer);

    gaussianFilterRGBA(d_img, d_result, d_temp, width, height, sigma, order, nthreads);
    checkCudaErrors(cudaDeviceSynchronize());
    getLastCudaError("Kernel execution failed");
    sdkStopTimer(&timer);

    unsigned char *h_result = (unsigned char *)malloc(width*height*4);
    checkCudaErrors(cudaMemcpy(h_result, d_result, width*height*4, cudaMemcpyDeviceToHost));

    char dump_file[1024];
    sprintf(dump_file, "lena_%02d.ppm", (int)sigma);
    sdkSavePPM4ub(dump_file, h_result, width, height);

    if (!sdkComparePPM(dump_file, sdkFindFilePath(ref_file, exec_path), MAX_EPSILON_ERROR, THRESHOLD, false))
    {
        nTotalErrors++;
    }

    printf("Processing time: %f (ms)\n", sdkGetTimerValue(&timer));
    printf("%.2f Mpixels/sec\n", (width*height / (sdkGetTimerValue(&timer) / 1000.0f)) / 1e6);

    checkCudaErrors(cudaFree(d_result));
    free(h_result);

    printf("Summary: %d errors!\n", nTotalErrors);

    printf(nTotalErrors == 0 ? "Test passed\n": "Test failed!\n");
    return (nTotalErrors == 0);
}

int
main(int argc, char **argv)
{
   pArgc = &argc;
   pArgv = argv;
    char *ref_file = NULL;

#if defined(__linux__)
    setenv ("DISPLAY", ":0", 0);
#endif

    printf("%s Starting...\n\n", sSDKsample);

    printf("NOTE: The CUDA Samples are not meant for performance measurements. Results may vary when GPU Boost is enabled.\n\n");

for(int n = 1;n<= 827; n++){
    // use command-line specified CUDA device, otherwise use device with highest Gflops/s
    if (argc > 1)
    {
        if (checkCmdLineFlag(argc, (const char **)argv, "file"))
        {
            getCmdLineArgumentString(argc, (const char **)argv, "file", &ref_file);
            fpsLimit = frameCheckNumber;
        }
    }

    // Get the path of the filename
    char *filename;

    if (getCmdLineArgumentString(argc, (const char **) argv, "image", &filename))
    {
        image_filename = filename;
    }


    // load image

char image_path[1024];
sprintf(image_path, "/home/wine/rgbd_dataset_freiburg3_walking_xyz/rgbppm/%d.ppm", n);

   // char *image_path = sdkFindFilePath(image_filename, argv[0]);
//    char *image_path = sdkFindFilePath(image_filename, argv[0]);

   /* if (image_path == NULL)
    {
        fprintf(stderr, "Error unable to find and load image file: '%s'\n", image_filename);//image_filename;
        exit(EXIT_FAILURE);
    }
*/
    sdkLoadPPM4ub(image_path, (unsigned char **)&h_img, &width, &height);
															
    if (!h_img)
    {
        printf("Error unable to load PPM file: '%s'\n", image_path);
        exit(EXIT_FAILURE);
    }

    printf("Loaded '%s', %d x %d pixels\n", image_path, width, height);

    if (checkCmdLineFlag(argc, (const char **)argv, "threads"))
    {
        nthreads = getCmdLineArgumentInt(argc, (const char **) argv, "threads");
    }

    if (checkCmdLineFlag(argc, (const char **)argv, "sigma"))
    {
        sigma = getCmdLineArgumentFloat(argc, (const char **) argv, "sigma");
    }

    runBenchmark = checkCmdLineFlag(argc, (const char **) argv, "benchmark");

    int device;
    struct cudaDeviceProp prop;
    cudaGetDevice(&device);
    cudaGetDeviceProperties(&prop, device);

    if (!strncmp("Tesla", prop.name, 5))
    {
        printf("Tesla card detected, running the test in benchmark mode (no OpenGL display)\n");
        //        runBenchmark = true;
        runBenchmark = true;
    }

    // Benchmark or AutoTest mode detected, no OpenGL
    if (runBenchmark == true || ref_file != NULL)
    {
        findCudaDevice(argc, (const char **)argv);
    }
    else
    {
        // First initialize OpenGL context, so we can properly set the GL for CUDA.
        // This is necessary in order to achieve optimal performance with OpenGL/CUDA interop.
//        initGL(&argc, argv);
//        findCudaGLDevice(argc, (const char **)argv);
    }

    initCudaBuffers();
   // initGLBuffers();
  //  glutMainLoop();

    // allocate memory for result
    unsigned int *d_result;
    unsigned int size = width * height * sizeof(unsigned int);
    checkCudaErrors(cudaMalloc((void **) &d_result, size));

    // warm-up
    gaussianFilterRGBA(d_img, d_result, d_temp, width, height, sigma, order, nthreads);

    checkCudaErrors(cudaDeviceSynchronize());
    unsigned char *h_result = (unsigned char *)malloc(width*height*4);
    checkCudaErrors(cudaMemcpy(h_result, d_result, width*height*4, cudaMemcpyDeviceToHost));

    char dump_file[1024];
    sprintf(dump_file, "%d.ppm",  n);
    sdkSavePPM4ub(dump_file, h_result, width, height);
cudaFree(d_result);
free(h_result);
}
    exit(EXIT_SUCCESS);
}
