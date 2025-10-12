#include "main.h"

Net *network;

/// load
extern "C" JNIEXPORT void JNICALL
Java_com_computer_vision_App_load(JNIEnv *env, jobject, jbyte ID, jobject jAssetManager) {
    AAssetManager *assetManager = AAssetManager_fromJava(env, jAssetManager);
    const auto has_gpu = get_gpu_count() > 0;
    network = new Net();
    network->opt.num_threads = get_cpu_count();
    network->opt.use_vulkan_compute = has_gpu;
    network->opt.lightmode = true; //recycle stuff, lower mem but lower perf
    network->opt.openmp_blocktime = 20; //keep cores enabled for 20ms after done to wait for more before shutting down
    network->opt.flush_denormals = 3; //smol af numbers = 0, higher perf but lower precision

    //gpu stuff, higher perf but higher mem
    network->opt.use_sgemm_convolution = true;
    network->opt.use_packing_layout = true;
    network->opt.use_local_pool_allocator = true;
    network->opt.use_winograd_convolution = true;
    network->opt.use_winograd23_convolution = true;
#if defined(__arm__) //too intensive for old devices, recognize by arm32
    network->opt.use_winograd43_convolution = false;
    network->opt.use_winograd63_convolution = false;
#else
    network->opt.use_winograd43_convolution = true;
    network->opt.use_winograd63_convolution = true;
#endif
    network->opt.use_bf16_storage = false;
    network->opt.use_fp16_uniform = false; //get_gpu_info().support_fp16_uniform();
    network->opt.use_fp16_packed = false; //get_gpu_info().support_fp16_packed();
    network->opt.use_fp16_storage = false; //get_gpu_info().support_fp16_storage();
    network->opt.use_fp16_arithmetic = false; //get_gpu_info().support_fp16_arithmetic();
//    network->opt.use_int8_inference = true;
    if (has_gpu) {
        network->opt.use_shader_pack8 = true;
        network->opt.use_shader_local_memory = true;
        network->opt.use_int8_uniform = get_gpu_info().support_int8_uniform();
        network->opt.use_int8_packed = get_gpu_info().support_int8_packed();
        network->opt.use_int8_storage = get_gpu_info().support_int8_storage();
        network->opt.use_int8_arithmetic = get_gpu_info().support_int8_arithmetic();
        network->opt.use_cooperative_matrix = get_gpu_info().support_cooperative_matrix();
        network->opt.use_subgroup_ballot = get_gpu_info().support_subgroup_ballot();
        network->opt.use_subgroup_basic = get_gpu_info().support_subgroup_basic();
        network->opt.use_subgroup_shuffle = get_gpu_info().support_subgroup_shuffle();
        network->opt.use_subgroup_vote = get_gpu_info().support_subgroup_vote();
        string n = get_gpu_info().device_name();
        transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n.find("adreno") != string::npos) {
            network->opt.use_image_storage = true;
            network->opt.use_tensor_storage = true;
        }
    }
    // in the .param, make sure to set the confidence threshold (2=) and nms threshold (3=) as scientific values after Yolov3DetectionOutput
    network->load_param(assetManager, (to_string(ID) + ".param").c_str());
    network->load_model(assetManager, (to_string(ID) + ".bin").c_str());
}

/// run
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_computer_vision_App_objectDetection(JNIEnv *env, jobject, jbyteArray image, jint imageWidth) {
    jint imageHeight = env->GetArrayLength(image) / (imageWidth * 4);
    jbyte *data = env->GetByteArrayElements(image, nullptr);
    Mat in = Mat::from_pixels_resize((const uint8_t *) data,
                                     Mat::PIXEL_RGBA2RGB,
                                     imageWidth, imageHeight,
                                     512, 288);
    // those are told by darknet2ncnn
    const float means[3] = {0, 0, 0};
    const float norms[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
    in.substract_mean_normalize(means, norms);
    Extractor ex = network->create_extractor();
    ex.input("data", in);
    Mat out;
    ex.extract("output", out);
    jobjectArray detectionsArray = env->NewObjectArray(out.h, env->FindClass("[I"), nullptr);
    // 0=class+1, 1=score, 2=xmin, 3=ymin, 4=xmax, 5=ymax
    // coords are normalized so multiply by image size for pixel coords
    for (int i = 0; i < out.h; i++) {
        const float label = out.row(i)[0] - 1;
        float x = out.row(i)[2] * float(imageWidth);
        float y = out.row(i)[3] * float(imageHeight);
        float width = out.row(i)[4] * float(imageWidth) - x;
        float height = out.row(i)[5] * float(imageHeight) - y;
        jintArray detection = env->NewIntArray(5);
        jint values[5] = {
            (jint) label,
            (jint) x,
            (jint) y,
            (jint) width,
            (jint) height
        };
        env->SetIntArrayRegion(detection, 0, 5, values);
        env->SetObjectArrayElement(detectionsArray, i, detection);
        env->DeleteLocalRef(detection);
    }
    env->ReleaseByteArrayElements(image, data, 0);
    return detectionsArray;
}