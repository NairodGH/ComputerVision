package com.computer.vision

import android.Manifest
import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.DialogInterface
import android.content.Intent
import android.graphics.Bitmap
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.OptIn
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.camera.view.TransformExperimental
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Face
import androidx.compose.material3.Button
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.core.graphics.createBitmap
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.lang.ref.WeakReference
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

class Act : ComponentActivity() {
    private val app by lazy { applicationContext as App }
    private lateinit var cameraPermissionLauncher: androidx.activity.result.ActivityResultLauncher<String>
    private val isProcessing = AtomicBoolean(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        cameraPermissionLauncher =
            registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
                if (granted) {
                    if (app.accessibilityRunning.get()) {
                        setCameraComposable()
                    } else {
                        requestAccessibilityPermission()
                    }
                } else {
                    requestCameraPermission()
                }
            }.apply { launch(Manifest.permission.CAMERA) }
    }

    override fun onResume() {
        super.onResume()
        app.act = WeakReference(this)
    }

    override fun onDestroy() {
        super.onDestroy()
        if (app.act.get() == this) app.act = WeakReference(null)
    }

    private fun requestAccessibilityPermission() {
        AlertDialog.Builder(this).setTitle(R.string.accessibility_required_title)
            .setMessage(R.string.accessibility_required_message)
            .setPositiveButton(R.string.ok) { _: DialogInterface, _: Int ->
                val accessibilityIntent = Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                val bundle = Bundle()
                val showArgs = packageName + "/" + Accessibility::class.java.name
                bundle.putString(":settings:fragment_args_key", showArgs)
                accessibilityIntent.putExtra(":settings:fragment_args_key", showArgs)
                accessibilityIntent.putExtra(":settings:show_fragment_args", bundle)
                startActivity(accessibilityIntent)
                app.coroutineMain.launch {
                    while (isActive) {
                        if (app.accessibilityRunning.get()) {
                            setCameraComposable()
                            val mainIntent = Intent(
                                this@Act, Act::class.java
                            )
                            mainIntent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
                            startActivity(mainIntent)
                            cancel()
                        } else {
                            delay(1000)
                        }
                    }
                }
            }.setCancelable(false).show()
    }

    private fun requestCameraPermission() {
        AlertDialog.Builder(this).setTitle(R.string.camera_required_title)
            .setMessage(R.string.camera_required_message)
            .setPositiveButton(R.string.ok) { _: DialogInterface, _: Int ->
                cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
            }.setCancelable(false).show()
    }

    private fun setCameraComposable() {
        setContent {
            CameraApp()
        }
        app.load(0, app.assets)
    }

    @SuppressLint("RestrictedApi")
    @OptIn(TransformExperimental::class)
    private fun processImage(imageProxy: ImageProxy, previewView: PreviewView) {
        if (isProcessing.get()) {
            imageProxy.close()
            return
        }

        app.coroutineDefault.launch {
            isProcessing.set(true)
            try {
                val buffer = imageProxy.planes[0].buffer
                val bytes = ByteArray(buffer.remaining())
                buffer.get(bytes)
//                download(bytes, imageProxy.width, imageProxy.height)
                val detections = app.objectDetection(bytes, imageProxy.width)
                app.accessibility.get()?.drawDetections(resize(imageProxy, previewView, detections))
            } catch (e: Exception) {
                e.printStackTrace()
            } finally {
                imageProxy.close()
                isProcessing.set(false)
            }
        }
    }

    private suspend fun resize(
        imageProxy: ImageProxy, previewView: PreviewView, detections: Array<IntArray>
    ): Array<IntArray> = withContext(Dispatchers.Main) {
        val sourceHeight = imageProxy.height.toFloat()
        Array(detections.size) { i ->
            val detection = detections[i]
            val scaleX = previewView.width.toFloat() / sourceHeight
            val scaleY = previewView.height.toFloat() / imageProxy.width.toFloat()
            intArrayOf(
                detection[0], // classId
                ((sourceHeight - detection[2] - detection[4]) * scaleX).toInt(), // rotated and scaled x
                (detection[1] * scaleY).toInt(), // scaled y
                (detection[4] * scaleX).toInt(), // scaled height
                (detection[3] * scaleY).toInt()  // scaled width
            )
        }
    }

    private fun download(bytes: ByteArray, width: Int, height: Int) {
        try {
            val bitmap = createBitmap(width, height)
            bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(bytes))
            val file =
                File(getExternalFilesDir(null), "${System.currentTimeMillis()}.png")
            FileOutputStream(file).use { out ->
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)
            }
            bitmap.recycle()
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    @Composable
    fun CameraApp() {
        val isBackCamera = remember { mutableStateOf(true) }
        Box(modifier = Modifier.fillMaxSize()) {
            AndroidView(
                factory = { ctx ->
                    val previewView = PreviewView(ctx)
                    previewView
                }, update = { previewView ->
                    setupCamera(previewView, isBackCamera.value)
                }, modifier = Modifier.fillMaxSize()
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(IntrinsicSize.Max)
                    .align(Alignment.TopCenter)
                    .padding(8.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(
                    onClick = { }, modifier = Modifier
                        .weight(1f)
                        .padding(2.dp)
                ) {
                    Text("Object detection")
                }
                Button(
                    onClick = { }, modifier = Modifier
                        .weight(1f)
                        .padding(2.dp)
                ) {
                    Text("Keypoint detection")
                }
                Button(
                    onClick = { }, modifier = Modifier
                        .weight(1f)
                        .padding(2.dp)
                ) {
                    Text("Instance segmentation")
                }
            }
            FloatingActionButton(
                onClick = {
                    isBackCamera.value = !isBackCamera.value
                }, modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .padding(16.dp)
            ) {
                Icon(Icons.Filled.Face, contentDescription = "Switch Camera")
            }
        }
    }

    @SuppressLint("RestrictedApi")
    private fun setupCamera(previewView: PreviewView, isBackCamera: Boolean) {
        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        cameraProviderFuture.addListener({
            val cameraProvider = cameraProviderFuture.get()
            val preview = Preview.Builder().build().also {
                it.surfaceProvider = previewView.surfaceProvider
            }
            val analysisUseCase = ImageAnalysis.Builder()
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build()
            analysisUseCase.setAnalyzer(ContextCompat.getMainExecutor(this)) { imageProxy ->
                processImage(imageProxy, previewView)
            }
            val cameraSelector = if (isBackCamera) CameraSelector.DEFAULT_BACK_CAMERA
            else CameraSelector.DEFAULT_FRONT_CAMERA
            try {
                cameraProvider.unbindAll()
                cameraProvider.bindToLifecycle(
                    this as LifecycleOwner, cameraSelector, preview, analysisUseCase
                )
            } catch (exc: Exception) {
                exc.printStackTrace()
            }

        }, ContextCompat.getMainExecutor(this))
    }
}