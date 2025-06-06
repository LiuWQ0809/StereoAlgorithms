#include"TRTFastACVNet_plus.h"
#include "ONNX2TRT.h"
FastACVNet_plus::FastACVNet_plus()
{

}
FastACVNet_plus::~FastACVNet_plus()
{

}
int FastACVNet_plus::Initialize(std::string model_path,int gpu_id,CalibrationParam&Calibrationparam)
{
    INPUT_H = 480;
    INPUT_W = 640;
    OUTPUT_SIZE= INPUT_H * INPUT_W;
    INPUT_BLOB_NAME1 = "left_image";
    INPUT_BLOB_NAME2 = "right_image";
    OUTPUT_BLOB_NAME = "output";
    this->Calibrationparam=Calibrationparam;
    char* trtModelStream{nullptr};
    size_t size{0};

    std::string directory; 
    const size_t last_slash_idx=model_path.rfind(".onnx");
    if (std::string::npos != last_slash_idx)
    {
        directory = model_path.substr(0, last_slash_idx);
    }
    std::string out_engine=directory+"_batch=1.engine";

    bool enginemodel=file_exists(out_engine);
    if (!enginemodel)
    {
        std::cout << "Building engine, please wait for a while..." << std::endl;
        bool onnx_model=file_exists(model_path);
        if (!onnx_model)
        {
           std::cout<<"stereo.onnx is not Exist!!!Please Check!"<<std::endl;
           return -1;
        }
        Onnx2Ttr onnx2trt;
		//IHostMemory* modelStream{ nullptr };
		onnx2trt.onnxToTRTModel(gLogger,model_path.c_str(),out_engine.c_str());
    }
    cudaSetDevice(gpu_id);
    std::ifstream file(out_engine, std::ios::binary);
    if (file.good())
    {
        file.seekg(0, file.end);
        size = file.tellg();
        file.seekg(0, file.beg);
        trtModelStream = new char[size];
        assert(trtModelStream && "trtModelStream == nullptr");
        file.read(trtModelStream, size);
        file.close();
    }
    else
    {
        return -1;
    }
    // init plugin
    bool didInitPlugins = initLibNvInferPlugins(nullptr, "");

    runtime = createInferRuntime(gLogger);
    assert(runtime != nullptr);
    engine = runtime->deserializeCudaEngine(trtModelStream, size);
    assert(engine != nullptr);
    context = engine->createExecutionContext();
    assert(context != nullptr);
    delete[] trtModelStream;
    // assert(engine->getNbBindings() == 3);
    assert(engine->getNbIOTensors() == 3);
    // In order to bind the buffers, we need to know the names of the input and output tensors.
    // Note that indices are guaranteed to be less than IEngine::getNbBindings()
    if (strcmp(engine->getIOTensorName(0), INPUT_BLOB_NAME1) == 0) {
        inputIndex1 = 0;
    }
    if (strcmp(engine->getIOTensorName(1), INPUT_BLOB_NAME2) == 0) {
        inputIndex2 = 1;
    }
    if (strcmp(engine->getIOTensorName(2), OUTPUT_BLOB_NAME) == 0) {
        outputIndex = 2;
    }
    // inputIndex1 = engine->getBindingIndex(INPUT_BLOB_NAME1);
    // inputIndex2 = engine->getBindingIndex(INPUT_BLOB_NAME2);
    // outputIndex = engine->getBindingIndex(OUTPUT_BLOB_NAME);
    assert(inputIndex1 == 0);
    assert(inputIndex2 == 1);
    assert(outputIndex == 2);

    CUDA_CHECK(cudaStreamCreate(&stream));
    // Create GPU buffers on device
    CUDA_CHECK(cudaMalloc((void**)&buffers[inputIndex1], CRESTEREO_BATCH_SIZE * 3 * INPUT_H * INPUT_W * sizeof(float)));
    CUDA_CHECK(cudaMalloc((void**)&buffers[inputIndex2], CRESTEREO_BATCH_SIZE * 3 * INPUT_H * INPUT_W * sizeof(float)));
    CUDA_CHECK(cudaMalloc((void**)&buffers[outputIndex], CRESTEREO_BATCH_SIZE * OUTPUT_SIZE * sizeof(float)));

    // prepare input data cache in pinned memory
    CUDA_CHECK(cudaMallocHost((void**)&img_left_host, INPUT_H * INPUT_W  * 3));
    CUDA_CHECK(cudaMallocHost((void**)&img_right_host, INPUT_H * INPUT_W  * 3));
    // prepare input data cache in device memory
    CUDA_CHECK(cudaMalloc((void**)&img_left_device, INPUT_H * INPUT_W  * 3));
    CUDA_CHECK(cudaMalloc((void**)&img_right_device, INPUT_H * INPUT_W  * 3));

    CUDA_CHECK(cudaMalloc((void**)&PointCloud_devide, INPUT_H*INPUT_W*6*sizeof(float)));  
    
    CUDA_CHECK(cudaMalloc((void**)&Q_device,16*sizeof(float)));
    cv::Matx44d _Q;
    this->Calibrationparam.Q.convertTo(_Q, CV_64F);
    cudaMallocManaged((void**)&Calibrationparam_Q,16*sizeof(float));
    for (size_t i = 0; i <16; i++)
    {
        Calibrationparam_Q[i]=(float)_Q.val[i];
    }
    //disparity   
    flow_up=new float[CRESTEREO_BATCH_SIZE * OUTPUT_SIZE];
    return 0;
}

int FastACVNet_plus::RunFastACVNet_plus(cv::Mat&rectifyImageL2,cv::Mat&rectifyImageR2,float*pointcloud,cv::Mat&DisparityMap)
{
    assert((INPUT_H == rectifyImageL2.rows) && (INPUT_H == rectifyImageR2.rows));
    assert((INPUT_W == rectifyImageL2.cols) && (INPUT_W == rectifyImageR2.cols));
    auto start = std::chrono::system_clock::now();
 
    //copy data to pinned memory
    memcpy(img_left_host,rectifyImageL2.data, 3*INPUT_H*INPUT_W*sizeof(uint8_t));
    memcpy(img_right_host,rectifyImageR2.data, 3*INPUT_H*INPUT_W*sizeof(uint8_t));
   
    //copy data to device memory
    CUDA_CHECK(cudaMemcpyAsync(img_left_device, img_left_host, 3*INPUT_H*INPUT_W*sizeof(uint8_t), cudaMemcpyHostToDevice,stream));
    CUDA_CHECK(cudaMemcpyAsync(img_right_device, img_right_host, 3*INPUT_H*INPUT_W*sizeof(uint8_t), cudaMemcpyHostToDevice,stream));

    FastACVNet_plus_preprocess(img_left_device, buffers[inputIndex1], INPUT_W, INPUT_H, stream);
    FastACVNet_plus_preprocess(img_right_device, buffers[inputIndex2], INPUT_W, INPUT_H, stream);

    // Set binding dimensions for input and output tensors
    for (int i = 0; i < engine->getNbIOTensors(); i++) {
        const char* tensorName = engine->getIOTensorName(i);
        if (strcmp(tensorName, INPUT_BLOB_NAME1) == 0) {
            context->setInputShape(INPUT_BLOB_NAME1, nvinfer1::Dims{4, {1, 3, INPUT_H, INPUT_W}});
	    context->setInputTensorAddress(INPUT_BLOB_NAME1, buffers[inputIndex1]);
        } else if (strcmp(tensorName, INPUT_BLOB_NAME2) == 0) {
            context->setInputShape(INPUT_BLOB_NAME2, nvinfer1::Dims{4, {1, 3, INPUT_H, INPUT_W}});
	    context->setInputTensorAddress(INPUT_BLOB_NAME2, buffers[inputIndex2]);
        }
    }

    // Set address for output tensor
    context->setOutputTensorAddress(OUTPUT_BLOB_NAME, buffers[outputIndex]);

    // Run inference
    bool success = context->enqueueV3(stream);
    assert(success);

    // Run inference
    //(*context).enqueue(CRESTEREO_BATCH_SIZE, (void**)buffers, stream, nullptr);
    // (*context).enqueueV2((void**)buffers, stream, nullptr);

    cudaStreamSynchronize(stream);
    FastACVNet_plus_reprojectImageTo3D(img_left_device,buffers[2],PointCloud_devide,Calibrationparam_Q,INPUT_H,INPUT_W);
    

    CUDA_CHECK(cudaMemcpy(flow_up, buffers[2], CRESTEREO_BATCH_SIZE * OUTPUT_SIZE * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(pointcloud,PointCloud_devide,INPUT_H*INPUT_W*6*sizeof(float),cudaMemcpyDeviceToHost));
    auto end = std::chrono::system_clock::now();
    std::cout<<"inference time:"<<(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count())<<"ms"<<std::endl;
    cv::Mat DisparityImage(INPUT_H,INPUT_W, CV_32FC1,flow_up);
    DisparityMap=DisparityImage.clone();
    return 0;
}
int FastACVNet_plus::Release()
{
    cudaStreamDestroy(stream);
    CUDA_CHECK(cudaFreeHost(img_left_host));
    CUDA_CHECK(cudaFreeHost(img_right_host));
    CUDA_CHECK(cudaFree(img_left_device));
    CUDA_CHECK(cudaFree(img_right_device));
    CUDA_CHECK(cudaFree(buffers[inputIndex1]));
    CUDA_CHECK(cudaFree(buffers[inputIndex2]));
    CUDA_CHECK(cudaFree(buffers[outputIndex]));
    CUDA_CHECK(cudaFree(PointCloud_devide));  
    CUDA_CHECK(cudaFree(Q_device));
    CUDA_CHECK(cudaFree(Calibrationparam_Q));
    //context->destroy();
    //engine->destroy();
    //runtime->destroy();
    delete context;
    delete engine;
    delete runtime;
    delete []flow_up;
    flow_up=NULL;
    return 0;
}