package io.github.spippoli.hipsterismo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CameraStatusTest {
    private val error = CameraStateError("CAMERA_IN_USE", critical = false)

    @Test
    fun theBackCameraIsPreferredUnlessTheFrontWasChosen() {
        assertEquals(Lens.BACK, cameraToBind(hasBack = true, hasFront = true, preferFront = false))
        assertEquals(Lens.FRONT, cameraToBind(hasBack = true, hasFront = true, preferFront = true))
        assertEquals(Lens.BACK, cameraToBind(hasBack = true, hasFront = false, preferFront = true))
        assertEquals(Lens.FRONT, cameraToBind(hasBack = false, hasFront = true, preferFront = false))
        assertNull(cameraToBind(hasBack = false, hasFront = false, preferFront = false))
    }

    @Test
    fun aSuccessfulBindIsOpenOnceTheCameraOpens() {
        var status: CameraStatus = CameraStatus.Opening
        status = nextCameraStatus(status, CameraEvent.Bound, 1)
        assertEquals(CameraStatus.Opening, status)
        status = nextCameraStatus(status, CameraEvent.StateChanged(CameraStateType.OPENING, null), 2)
        assertEquals(CameraStatus.Opening, status)
        status = nextCameraStatus(status, CameraEvent.StateChanged(CameraStateType.OPEN, null), 3)
        assertEquals(CameraStatus.Open, status)
    }

    @Test
    fun aBindExceptionFails() {
        val status = nextCameraStatus(
            CameraStatus.Opening,
            CameraEvent.BindFailed("IllegalArgumentException", "No available camera can be found"),
            42,
        )
        assertEquals(
            CameraStatus.Failed(
                CameraError(CameraErrorKind.BIND_FAILED, "IllegalArgumentException: No available camera can be found", 42),
            ),
            status,
        )
    }

    @Test
    fun aBindExceptionWithoutMessageNamesItsClass() {
        val status = nextCameraStatus(CameraStatus.Opening, CameraEvent.BindFailed("IllegalStateException", null), 7)
        assertEquals("IllegalStateException", (status as CameraStatus.Failed).error.message)
    }

    @Test
    fun aMissingCameraFails() {
        val status = nextCameraStatus(CameraStatus.Opening, CameraEvent.NoCamera, 5)
        assertEquals(
            CameraStatus.Failed(CameraError(CameraErrorKind.NO_CAMERA, "no back or front camera", 5)),
            status,
        )
    }

    @Test
    fun aCameraStateErrorAfterABindFails() {
        var status: CameraStatus = CameraStatus.Opening
        status = nextCameraStatus(status, CameraEvent.Bound, 1)
        status = nextCameraStatus(status, CameraEvent.StateChanged(CameraStateType.OPEN, null), 2)
        status = nextCameraStatus(status, CameraEvent.StateChanged(CameraStateType.PENDING_OPEN, error), 3)
        assertEquals(
            CameraStatus.Failed(CameraError(CameraErrorKind.CAMERA_STATE, "PENDING_OPEN: CAMERA_IN_USE (recoverable)", 3)),
            status,
        )
        val critical = nextCameraStatus(
            CameraStatus.Open,
            CameraEvent.StateChanged(CameraStateType.CLOSED, CameraStateError("CAMERA_FATAL_ERROR", critical = true)),
            4,
        )
        assertEquals("CLOSED: CAMERA_FATAL_ERROR (critical)", (critical as CameraStatus.Failed).error.message)
    }

    @Test
    fun closingWithoutAnErrorIsNotAFailure() {
        // The lifecycle closes the camera in the background and reopens it on return.
        var status: CameraStatus = CameraStatus.Open
        for (type in listOf(CameraStateType.CLOSING, CameraStateType.CLOSED, CameraStateType.PENDING_OPEN, CameraStateType.OPENING)) {
            status = nextCameraStatus(status, CameraEvent.StateChanged(type, null), 1)
            assertEquals(CameraStatus.Open, status)
        }
    }

    @Test
    fun aFailureLastsUntilTheCameraOpens() {
        val failed = nextCameraStatus(CameraStatus.Open, CameraEvent.StateChanged(CameraStateType.PENDING_OPEN, error), 3)
        // CameraX retrying by itself: still the error screen...
        val retrying = nextCameraStatus(failed, CameraEvent.StateChanged(CameraStateType.OPENING, null), 4)
        assertEquals(failed, retrying)
        // ...until the camera is free again.
        assertEquals(CameraStatus.Open, nextCameraStatus(retrying, CameraEvent.StateChanged(CameraStateType.OPEN, null), 5))
    }

    @Test
    fun retryRecoversOnceTheCameraIsFree() {
        var status = nextCameraStatus(CameraStatus.Opening, CameraEvent.BindFailed("CameraUnavailableException", "busy"), 1)
        status = nextCameraStatus(status, CameraEvent.Retry, 2)
        assertEquals(CameraStatus.Opening, status)
        status = nextCameraStatus(status, CameraEvent.Bound, 3)
        status = nextCameraStatus(status, CameraEvent.StateChanged(CameraStateType.OPEN, null), 4)
        assertEquals(CameraStatus.Open, status)
    }

    @Test
    fun aRetryThatFailsAgainKeepsTheNewError() {
        var status = nextCameraStatus(CameraStatus.Opening, CameraEvent.NoCamera, 1)
        status = nextCameraStatus(status, CameraEvent.Retry, 2)
        status = nextCameraStatus(status, CameraEvent.BindFailed("IllegalArgumentException", "x"), 3)
        assertEquals(CameraError(CameraErrorKind.BIND_FAILED, "IllegalArgumentException: x", 3), (status as CameraStatus.Failed).error)
    }

    @Test
    fun theLastErrorIsKept() {
        val log = LastCameraError()
        assertNull(log.value)
        var status: CameraStatus = CameraStatus.Opening
        status = log.track(nextCameraStatus(status, CameraEvent.NoCamera, 1))
        val first = log.value
        assertEquals(CameraErrorKind.NO_CAMERA, first?.kind)
        // A recovery keeps the error for the report.
        status = log.track(nextCameraStatus(status, CameraEvent.Retry, 2))
        status = log.track(nextCameraStatus(status, CameraEvent.StateChanged(CameraStateType.OPEN, null), 3))
        assertEquals(CameraStatus.Open, status)
        assertEquals(first, log.value)
        log.track(nextCameraStatus(status, CameraEvent.StateChanged(CameraStateType.CLOSED, error), 4))
        assertEquals(4L, log.value?.atMillis)
    }
}
