package com.computer.vision

import android.accessibilityservice.AccessibilityService
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.PorterDuff
import android.graphics.Rect
import android.graphics.Typeface
import android.os.Build
import android.util.DisplayMetrics
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.view.accessibility.AccessibilityEvent
import java.lang.ref.WeakReference
import java.util.concurrent.atomic.AtomicInteger

@SuppressLint("AccessibilityPolicy")
class Accessibility : AccessibilityService() {
    private val app by lazy { applicationContext as App }
    private lateinit var windowManager: WindowManager
    private lateinit var detectionsView: DetectionsView

    /// DetectionsView
    class DetectionsView(context: Context) : View(context) {
        val detections: MutableList<IntArray> = ArrayList()
        private val cheatClassColorMap: MutableMap<String, Int> = HashMap()
        private val colors = intArrayOf(
            Color.RED,
            Color.GREEN,
            Color.BLUE,
            Color.YELLOW,
            Color.CYAN,
            Color.MAGENTA,
            Color.WHITE,
            Color.LTGRAY,
            Color.DKGRAY
        )
        private val boxPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = 5f
        }
        private val labelBackgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.FILL
        }
        private val labelTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.BLACK
            textSize = 40f
            typeface = Typeface.DEFAULT_BOLD
        }
        private val colorIndex = AtomicInteger()
        private val classNames = mapOf(
            0 to "steve",
            1 to "sword",
            2 to "dirt",
            3 to "enderman"
        )

        @SuppressLint("DrawAllocation")
        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)
            synchronized(detections) {
                for (detection in detections) {
                    if (detection.size >= 5) {
                        val classId = detection[0]
                        val x = detection[1].toFloat()
                        val y = detection[2].toFloat()
                        val width = detection[3].toFloat()
                        val height = detection[4].toFloat()
                        val color = cheatClassColorMap.getOrPut("$classId") {
                            colors[colorIndex.getAndIncrement() % colors.size]
                        }
                        boxPaint.color = color
                        canvas.drawRect(x, y, x + width, y + height, boxPaint)
                        val label = classNames[classId] ?: "unknown"
                        val textBounds = Rect().apply {
                            labelTextPaint.getTextBounds(
                                label,
                                0,
                                label.length,
                                this
                            )
                        }
                        val textWidth = textBounds.width() + 16f
                        val textHeight = textBounds.height() + 16f
                        val labelY = (y - textHeight).coerceAtLeast(0f)
                        labelBackgroundPaint.color = color
                        canvas.drawRect(
                            x,
                            labelY,
                            x + textWidth,
                            labelY + textHeight,
                            labelBackgroundPaint
                        )
                        canvas.drawText(
                            label,
                            x + 8f,
                            labelY + textHeight - 8f,
                            labelTextPaint
                        )
                    }
                }
            }
        }

        override fun onDetachedFromWindow() {
            super.onDetachedFromWindow()
            detections.clear()
            cheatClassColorMap.clear()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onServiceConnected() {
        super.onServiceConnected()
        app.accessibility = WeakReference(this)
        app.accessibilityRunning.set(true)
        windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
        val metrics = DisplayMetrics()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val windowMetrics = windowManager.currentWindowMetrics
            val bounds = windowMetrics.bounds
            metrics.widthPixels = bounds.width()
            metrics.heightPixels = bounds.height()
        } else {
            @Suppress("DEPRECATION") windowManager.defaultDisplay.getRealMetrics(metrics)
        }
        detectionsView = DetectionsView(this)
        val params = WindowManager.LayoutParams(
            metrics.widthPixels,
            metrics.heightPixels,
            WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 0
            y = 0
        }
        windowManager.addView(detectionsView, params)
    }

    fun drawDetections(detections: Array<IntArray>) {
        detectionsView.detections.clear()
        synchronized(detectionsView.detections) {
            for (detection in detections) {
                detectionsView.detections.add(detection.copyOf())
            }
        }
        detectionsView.postInvalidate()
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent) {
    }

    override fun onInterrupt() {
    }

    override fun onUnbind(intent: Intent?): Boolean {
        app.accessibilityRunning.set(false)
        return super.onUnbind(intent)
    }

    override fun onDestroy() {
        app.accessibilityRunning.set(false)
        super.onDestroy()
    }
}