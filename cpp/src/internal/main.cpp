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

        if (params[0] == 1) { // Object detection
            // Denormalize coordinates to 640x480 letterboxed image space
            float x = row[2] * 640.f;
            float y = row[3] * 480.f;
            float w = row[4] * 640.f - x;
            float h = row[5] * 480.f - y;
            // Rotate 90° clockwise and scale to screen resolution
            processed = {
                row[0] - 1,
                (480.f - y - h) * scaleX,
                x * scaleY,
                h * scaleX,
                w * scaleY
            };
        } else if (params[0] == 2) { // Keypoint detection
            if (i == 0) {
                const int nbKeypoints = 8;
                float bestConfidence = 0.f;
                float bestKeypoints[nbKeypoints * 2];
                // Iterate through all grid cells in all FPN levels
                // Total positions: 80*80 + 40*40 + 20*20 = 8400
                int pos = 0;
                for (int level: {80, 40, 20}) {
                    for (int gridY = 0; gridY < level; gridY++) {
                        for (int gridX = 0; gridX < level; gridX++, pos++) {
                            float confidence = out.row(4)[pos];
                            if (confidence <= bestConfidence || confidence < 0.7f)
                                continue; // Skip if not better or too low
                            bestConfidence = confidence;
                            // Extract keypoint coordinates for this detection
                            // Rows 5-20: x0,y0,x1,y1,...,x7,y7 (8 keypoints, 16 values)
                            for (int j = 0; j < nbKeypoints; j++) {
                                bestKeypoints[j * 2] = out.row(5 + j * 2)[pos];
                                bestKeypoints[j * 2 + 1] = out.row(6 + j * 2)[pos];
                            }
                        }
                    }
                }
                if (bestConfidence > 0.f) {
                    vector<float> keypoints;
                    keypoints.reserve(nbKeypoints * 2);
                    for (int j = 0; j < nbKeypoints; j++) {
                        // Rotate 90° clockwise and scale to screen resolution
                        keypoints.push_back((480.f - bestKeypoints[j * 2 + 1] - float(pad)) * scaleX);
                        keypoints.push_back(bestKeypoints[j * 2] * scaleY);
                    }
                    processed = keypoints;
                } else {
                    processed.clear(); // No detection above threshold
                }
            } else {
                processed.clear(); // Skip rows after first (detect only 1 object)
            }
        } else if (params[0] == 3) { // Instance segmentation
            Mat proto;
            ex.extract("proto", proto); // Extract prototype masks (32x160x160 tensor, only for seg)

            if (i == 0) { // Only process first row (subsequent rows are empty for seg)
                float bestConfidence = 0.f;
                int bestID = -1;
                // Find detection with highest confidence across all 8400 anchor points
                for (int col = 0; col < out.w; col++) {
                    float conf = out.row(4)[col]; // Row 4 contains objectness scores
                    if (conf > bestConfidence) {
                        bestConfidence = conf;
                        bestID = col;
                    }
                }
                if (bestConfidence > 0.5f && bestID >= 0) {
                    float x = out.row(0)[bestID];
                    float y = out.row(1)[bestID];
                    float w = out.row(2)[bestID];
                    float h = out.row(3)[bestID];
                    // Generate mask by combining proto and mask coefficients
                    Mat mask(160, 160, 1);
                    for (int my = 0; my < 160; my++) {
                        for (int mx = 0; mx < 160; mx++) {
                            float val = 0.f;
                            // Linear combination: sum(coeff[i] * proto[i][y][x]) for all 32 channels
                            // Rows 5-36 contains the 32 mask coefficients
                            for (int c = 0; c < 32; c++) {
                                val += out.row(5 + c)[bestID] * proto.channel(c).row(my)[mx];
                            }
                            // Apply sigmoid activation to get probability
                            mask.row(my)[mx] = 1.f / (1.f + expf(-val));
                        }
                    }
                    // Convert bounding box from 640x640 to 160x160 mask space
                    // This crops the mask to just the object region to reduce noise
                    int x1 = max(0, (int) ((x - w / 2.f) * 160.f / 640.f));
                    int y1 = max(0, (int) ((y - h / 2.f) * 160.f / 640.f));
                    int x2 = min(160, (int) ((x + w / 2.f) * 160.f / 640.f));
                    int y2 = min(160, (int) ((y + h / 2.f) * 160.f / 640.f));
                    // Extract edge/contour points from the mask
                    vector<pair<float, float>> edgePoints;
                    float sumX = 0, sumY = 0;
                    for (int my = y1; my < y2; my++) {
                        for (int mx = x1; mx < x2; mx++) {
                            // Pixel is part of object
                            if (mask.row(my)[mx] > 0.5f) {
                                // Check if this pixel is on the edge (has a neighbor below threshold)
                                if (mx == x1 || mx == x2 - 1 || my == y1 || my == y2 - 1 ||
                                    mask.row(my - 1)[mx] <= 0.5f || mask.row(my + 1)[mx] <= 0.5f ||
                                    mask.row(my)[mx - 1] <= 0.5f || mask.row(my)[mx + 1] <= 0.5f) {
                                    edgePoints.emplace_back(mx, my);
                                    sumX += float(mx);
                                    sumY += float(my);
                                }
                            }
                        }
                    }
                    if (edgePoints.size() >= 18) { // Polygon I annotated is 18 points
                        // Calculate centroid of all edge points
                        float centroidX = sumX / float(edgePoints.size());
                        float centroidY = sumY / float(edgePoints.size());
                        // Sort edge points by angle from centroid (clockwise)
                        // This converts random pixel order into a proper contour sequence
                        sort(edgePoints.begin(), edgePoints.end(),
                             [centroidX, centroidY](const pair<float, float> &a, const pair<float, float> &b) {
                                 return atan2(a.second - centroidY, a.first - centroidX) <
                                        atan2(b.second - centroidY, b.first - centroidX);
                             });
                        // Subsample to exactly 18 points by dividing 360° into 18 sectors
                        vector<float> polygon;
                        for (int j = 0; j < 18; j++) {
                            // Calculate target angle for this sector (-π to +π)
                            float targetAngle = -M_PI + (float(j) * 2.f * M_PI / 18.f);
                            // Find edge point closest to target angle
                            int bestPoint = 0;
                            float minDiff = 999.f;
                            for (int k = 0; k < edgePoints.size(); k++) {
                                float angle = atan2(edgePoints[k].second - centroidY,
                                                    edgePoints[k].first - centroidX);
                                float diff = abs(angle - targetAngle);
                                if (diff < minDiff) {
                                    minDiff = diff;
                                    bestPoint = k;
                                }
                            }
                            // Scale from 160x160 mask to 640x640 image space (*4)
                            float px = edgePoints[bestPoint].first * 4.f;
                            float py = edgePoints[bestPoint].second * 4.f - float(pad);
                            // Remove letterbox padding (subtract pad from y)
                            // Rotate 90° and scale to screen coordinates
                            polygon.push_back((480.f - py) * scaleX);
                            polygon.push_back(px * scaleY);
                        }
                        processed = polygon;
                    } else {
                        processed.clear(); // Not enough points for valid polygon
                    }
                } else {
                    processed.clear(); // No confident detection
                }
            } else {
                processed.clear(); // Skip rows after first (detect only 1 object)
            }
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