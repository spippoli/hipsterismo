package io.github.spippoli.hipsterismo

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.view.PreviewView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver

/**
 * The app opens on the camera screen ([MosaicApp]). Without the CAMERA permission it asks
 * once, and while the permission is denied it shows why it is needed.
 *
 * The screen stays in portrait, as in the stock camera app: held in
 * landscape, only the icons turn. The activity still handles configuration
 * changes itself (`configChanges`), so the camera keeps running.
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        // A shutter JPEG left over from a photo that never finished, and the
        // PNG of the last Condividi (#58).
        if (savedInstanceState == null) {
            photoCache(this).deleteRecursively()
            MosaicExport.shareCache(this).deleteRecursively()
        }
        val previewView = PreviewView(this).apply {
            scaleType = PreviewView.ScaleType.FIT_CENTER
            implementationMode = PreviewView.ImplementationMode.COMPATIBLE
        }
        setContent {
            var granted by remember { mutableStateOf(hasCamera()) }
            var denied by rememberSaveable { mutableStateOf(false) }
            var refusals by rememberSaveable { mutableIntStateOf(0) }
            val request = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
                granted = ok
                denied = !ok
                if (!ok) refusals++
            }
            // After a refusal Android shows the request again while the
            // rationale flag is set; after the second (or a dismissed dialog
            // and a refusal) only the settings can grant it.
            val canAsk = refusals < 2 || shouldShowRequestPermissionRationale(Manifest.permission.CAMERA)
            LaunchedEffect(Unit) {
                if (!granted && !denied) request.launch(Manifest.permission.CAMERA)
            }
            // Back from the settings, or the permission revoked meanwhile.
            DisposableEffect(Unit) {
                val observer = LifecycleEventObserver { _, event ->
                    if (event == Lifecycle.Event.ON_RESUME) granted = hasCamera()
                }
                lifecycle.addObserver(observer)
                onDispose { lifecycle.removeObserver(observer) }
            }

            when {
                granted -> {
                    val pipeline = remember { PreviewPipeline(this) }
                    DisposableEffect(pipeline) {
                        pipeline.start(this@MainActivity, previewView.surfaceProvider)
                        onDispose { pipeline.stop() }
                    }
                    MosaicApp(pipeline, previewView)
                }
                denied -> PermissionScreen(canAsk) {
                    if (canAsk) request.launch(Manifest.permission.CAMERA) else openAppSettings()
                }
                else -> Box(Modifier.fillMaxSize().background(PanelDark))
            }
        }
    }

    private fun hasCamera() = checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

    private fun openAppSettings() {
        startActivity(
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", packageName, null)),
        )
    }
}
