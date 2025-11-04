package com.computer.vision

import android.app.Application
import android.content.res.AssetManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import java.lang.ref.WeakReference
import java.util.concurrent.atomic.AtomicBoolean

class App : Application() {
    lateinit var act: WeakReference<Act>
    lateinit var accessibility: WeakReference<Accessibility>
    var coroutineMain = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    var coroutineDefault = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    val accessibilityRunning: AtomicBoolean by lazy { AtomicBoolean(false) }
    var currentID = 1
    external fun load(id: Int, assetManager: AssetManager)
    external fun run(image: ByteArray, width: Int): Array<FloatArray>

    override fun onCreate() {
        super.onCreate()
        System.loadLibrary("ComputerVision")
    }
}