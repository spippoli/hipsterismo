package io.github.spippoli.hipsterismo

/*
 * Whether the camera works, as a pure function of what happened to it (#104):
 * the camera screen shows while the camera is opening or open, the
 * camera-error screen while it has failed. No Android classes, so the JVM
 * tests reach it; PreviewPipeline turns CameraX's exceptions and camera
 * states into [CameraEvent]s.
 */

/** A camera to bind. */
enum class Lens { BACK, FRONT }

/**
 * The camera to bind: the front one if [preferFront] and the phone has it,
 * else the back one, else the front one; null if it has neither (only an
 * external camera, or none).
 */
fun cameraToBind(hasBack: Boolean, hasFront: Boolean, preferFront: Boolean): Lens? = when {
    hasFront && (preferFront || !hasBack) -> Lens.FRONT
    hasBack -> Lens.BACK
    else -> null
}

/** What went wrong with the camera. */
enum class CameraErrorKind {
    /** The provider, the presence check or the lifecycle bind threw. */
    BIND_FAILED,

    /** The phone has neither a back nor a front camera. */
    NO_CAMERA,

    /** The bound camera reported an error (another app took it, a HAL error). */
    CAMERA_STATE,
}

/** A camera error, for the error screen and the diagnostic report; [atMillis] is wall-clock time. */
data class CameraError(val kind: CameraErrorKind, val message: String, val atMillis: Long)

/** CameraX's `CameraState.Type`, by name. */
enum class CameraStateType { PENDING_OPEN, OPENING, OPEN, CLOSING, CLOSED }

/** CameraX's `CameraState.StateError`: its code's name (`CAMERA_IN_USE`) and whether it is critical. */
data class CameraStateError(val name: String, val critical: Boolean)

/** Something that happened to the camera. */
sealed interface CameraEvent {
    /** The bind went through; the camera is opening. */
    data object Bound : CameraEvent

    /** The bind (or getting the provider, or the presence check) threw [exception] with [message]. */
    data class BindFailed(val exception: String, val message: String?) : CameraEvent

    /** Neither a back nor a front camera. */
    data object NoCamera : CameraEvent

    /** The bound camera's state, with its [error] if it has one. */
    data class StateChanged(val type: CameraStateType, val error: CameraStateError?) : CameraEvent

    /** Retry was tapped: the camera is bound again. */
    data object Retry : CameraEvent
}

/** Whether the camera works. */
sealed interface CameraStatus {
    /** Starting, binding or opening: the camera screen, waiting for its first Mosaic. */
    data object Opening : CameraStatus

    /** The camera is open. */
    data object Open : CameraStatus

    /** The camera-error screen, for [error]. */
    data class Failed(val error: CameraError) : CameraStatus
}

/**
 * The status after [event], at [nowMillis]. A failure lasts until Retry or
 * until the camera opens again (CameraX reopens it by itself once another
 * app frees it); a camera closing without an error (the app in the
 * background) changes nothing.
 */
fun nextCameraStatus(status: CameraStatus, event: CameraEvent, nowMillis: Long): CameraStatus = when (event) {
    CameraEvent.Retry -> CameraStatus.Opening
    CameraEvent.Bound -> if (status is CameraStatus.Open) status else CameraStatus.Opening
    CameraEvent.NoCamera -> CameraStatus.Failed(CameraError(CameraErrorKind.NO_CAMERA, "no back or front camera", nowMillis))
    is CameraEvent.BindFailed -> CameraStatus.Failed(
        CameraError(
            CameraErrorKind.BIND_FAILED,
            if (event.message.isNullOrEmpty()) event.exception else "${event.exception}: ${event.message}",
            nowMillis,
        ),
    )
    is CameraEvent.StateChanged -> when {
        event.error != null -> CameraStatus.Failed(
            CameraError(
                CameraErrorKind.CAMERA_STATE,
                "${event.type}: ${event.error.name} (${if (event.error.critical) "critical" else "recoverable"})",
                nowMillis,
            ),
        )
        event.type == CameraStateType.OPEN -> CameraStatus.Open
        else -> status
    }
}

/**
 * The last camera error since the app started, for the diagnostic report
 * (#7): kept through a recovery, replaced by the next error.
 */
class LastCameraError {
    @Volatile
    var value: CameraError? = null
        private set

    /** Keeps [status]'s error if it has one, and returns [status]. */
    fun track(status: CameraStatus): CameraStatus {
        if (status is CameraStatus.Failed) value = status.error
        return status
    }

    companion object {
        /** The app's one record, which the pipeline writes and any code may read. */
        val app = LastCameraError()
    }
}
