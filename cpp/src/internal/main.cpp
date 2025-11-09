#include "main.h"

Net *network = nullptr;

/// load
extern "C" JNIEXPORT void JNICALL
Java_com_computer_vision_App_load(JNIEnv *env, jobject, jint id, jobject jAssetManager) {
    AAssetManager *assetManager = AAssetManager_fromJava(env, jAssetManager);
    const auto has_gpu = get_gpu_count() > 0;
    if (network != nullptr) {
        delete network;
        network = nullptr;
    }
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
    network->load_param(assetManager, (to_string(id) + ".param").c_str());
    network->load_model(assetManager, (to_string(id) + ".bin").c_str());
}

/// run
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_computer_vision_App_run(JNIEnv *env, jobject, jbyteArray jBytes, jintArray jParams) {
    jbyte *bytes = env->GetByteArrayElements(jBytes, nullptr);
    jint *params = env->GetIntArrayElements(jParams, nullptr);

    //letterbox 640x480 camera feed to 640x640 images like the ones the model was trained on (to avoid stretching)
    Mat original = Mat::from_pixels((const uint8_t *) bytes, Mat::PIXEL_RGBA2RGB, 640, 480);
    int pad = (640 - 480) / 2;
    Mat letterboxed(640, 640, 3);
    letterboxed.fill(114.f);
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < 480; y++) {
            float *dst_ptr = letterboxed.channel(c).row(y + pad);
            const float *src_ptr = original.channel(c).row(y);
            memcpy(dst_ptr, src_ptr, 640 * sizeof(float));
        }
    }

    // those are told by darknet2ncnn
    const float means[3] = {0, 0, 0};
    const float norms[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
    letterboxed.substract_mean_normalize(means, norms);
    Extractor ex = network->create_extractor();
    ex.input("data", letterboxed);
    Mat out;
    ex.extract("output", out);

    jobjectArray detectionsArray = env->NewObjectArray(out.h, env->FindClass("[F"), nullptr);
    const float scaleX = (float) params[1] / 480.f;
    const float scaleY = (float) params[2] / 640.f;
    for (int i = 0; i < out.h; i++) {
        const float *row = out.row(i);
        vector<float> processed(row, row + out.w);

        if (params[0] == 1) { // Object detection, normalized to pixels, scale and rotate
            float x = row[2] * 640.f;
            float y = row[3] * 480.f;
            float w = row[4] * 640.f - x;
            float h = row[5] * 480.f - y;
            float rx = (480.f - y - h) * scaleX;
            float ry = x * scaleY;
            float rw = h * scaleX;
            float rh = w * scaleY;
            processed = {row[0] - 1, rx, ry, rw, rh};
        } else if (params[0] == 2) { // Keypoint detection, extract keypoints and rotate
            if (i == 0) {
                const int num_keypoints = 8;
                const int num_levels = 3;
                const float stride_list[3] = {8.f, 16.f, 32.f};
                const int level_sizes[3] = {80, 40, 20};
                const float conf_threshold = 0.7f;
                float best_conf = 0.f;
                float best_kps[num_keypoints * 2];
                int pos = 0;
                for (int s : level_sizes) {
                    for (int gy = 0; gy < s; gy++) {
                        for (int gx = 0; gx < s; gx++, pos++) {
                            float conf = out.row(4)[pos];
                            if (conf <= best_conf || conf < conf_threshold)
                                continue;
                            best_conf = conf;
                            for (int k = 0; k < num_keypoints; k++) {
                                best_kps[k * 2] = out.row(5 + k * 2)[pos];
                                best_kps[k * 2 + 1] = out.row(6 + k * 2)[pos];
                            }
                        }
                    }
                }
                if (best_conf > 0.f) {
                    vector<float> keypoints;
                    keypoints.reserve(num_keypoints * 2);

                    for (int k = 0; k < num_keypoints; k++) {
                        float px = best_kps[k * 2];
                        float py = best_kps[k * 2 + 1] - pad;
                        float rotated_x = (480.f - py) * scaleX;
                        float rotated_y = px * scaleY;
                        keypoints.push_back(rotated_x);
                        keypoints.push_back(rotated_y);
                    }
                    processed = keypoints;
                } else {
                    processed.clear();
                }
            } else {
                processed.clear();
            }
        } else if (params[0] == 3) { // Instance segmentation

        }
        jfloatArray rowArray = env->NewFloatArray((jsize) processed.size());
        if (!processed.empty()) {
            env->SetFloatArrayRegion(rowArray, 0, (jsize) processed.size(), processed.data());
        }
        env->SetObjectArrayElement(detectionsArray, i, rowArray);
        env->DeleteLocalRef(rowArray);
    }

    env->ReleaseByteArrayElements(jBytes, bytes, 0);
    env->ReleaseIntArrayElements(jParams, params, 0);
    return detectionsArray;
}