from __future__ import annotations

import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

from pc_mjpeg_receiver import (  # noqa: E402
    MjpegPart,
    MultipartEnd,
    MultipartReader,
    ReceiverError,
    ReceiverStatistics,
    extract_multipart_boundary,
    format_terminal_summary,
    write_json_report,
)


BOUNDARY = b"d2frame"
JPEG = b"\xff\xd8payload\xff\xd9"


class FragmentedRaw(io.RawIOBase):
    def __init__(self, data: bytes, fragment_sizes: tuple[int, ...]) -> None:
        self._data = data
        self._position = 0
        self._fragment_sizes = fragment_sizes
        self._fragment_index = 0

    def readable(self) -> bool:
        return True

    def readinto(self, buffer: bytearray) -> int:
        if self._position >= len(self._data):
            return 0
        fragment_size = self._fragment_sizes[
            self._fragment_index % len(self._fragment_sizes)
        ]
        self._fragment_index += 1
        count = min(fragment_size, len(buffer), len(self._data) - self._position)
        buffer[:count] = self._data[self._position : self._position + count]
        self._position += count
        return count


def part(
    frame_id: int = 42,
    payload: bytes = JPEG,
    extra_headers: bytes = b"",
    content_length: int | None = None,
) -> bytes:
    length = len(payload) if content_length is None else content_length
    return (
        b"--d2frame\r\n"
        b"Content-Type: image/jpeg\r\n"
        + f"Content-Length: {length}\r\n".encode("ascii")
        + f"X-Frame-Id: {frame_id}\r\n".encode("ascii")
        + b"X-Capture-Timestamp-Us: 123456789\r\n"
        + extra_headers
        + b"\r\n"
        + payload
        + b"\r\n"
    )


def fragmented_stream(data: bytes) -> io.BufferedReader:
    return io.BufferedReader(
        FragmentedRaw(data, (1, 2, 7, 3, 11)), buffer_size=5
    )


class MultipartReaderTests(unittest.TestCase):
    def test_extracts_quoted_boundary_case_insensitively(self) -> None:
        boundary = extract_multipart_boundary(
            'Multipart/X-Mixed-Replace; charset=UTF-8; boundary="d2frame"'
        )
        self.assertEqual(boundary, BOUNDARY)

    def test_rejects_missing_boundary(self) -> None:
        with self.assertRaisesRegex(ReceiverError, "missing its boundary"):
            extract_multipart_boundary("multipart/x-mixed-replace")

    def test_reads_multiple_parts_across_arbitrary_fragments(self) -> None:
        stream = fragmented_stream(part(42) + part(43) + b"--d2frame--\r\n")
        reader = MultipartReader(stream, BOUNDARY)

        first = reader.read_part()
        second = reader.read_part()
        self.assertEqual(first.frame_id, 42)
        self.assertEqual(first.capture_timestamp_us, 123456789)
        self.assertEqual(first.payload, JPEG)
        self.assertEqual(second.frame_id, 43)
        with self.assertRaises(MultipartEnd):
            reader.read_part()

    def test_accepts_case_insensitive_part_headers(self) -> None:
        data = part().replace(b"Content-Type", b"content-type").replace(
            b"X-Frame-Id", b"x-frame-id"
        )
        parsed = MultipartReader(fragmented_stream(data), BOUNDARY).read_part()
        self.assertEqual(parsed.frame_id, 42)

    def test_rejects_missing_required_header(self) -> None:
        data = part().replace(b"X-Frame-Id: 42\r\n", b"")
        with self.assertRaisesRegex(ReceiverError, "X-Frame-Id"):
            MultipartReader(fragmented_stream(data), BOUNDARY).read_part()

    def test_rejects_oversized_content_length_before_payload_read(self) -> None:
        data = part(content_length=1000)
        with self.assertRaisesRegex(ReceiverError, "outside the accepted range"):
            MultipartReader(
                fragmented_stream(data), BOUNDARY, max_jpeg_bytes=100
            ).read_part()

    def test_rejects_truncated_payload(self) -> None:
        data = part(content_length=len(JPEG) + 10)[:-2]
        with self.assertRaisesRegex(ReceiverError, "stream ended during JPEG payload"):
            MultipartReader(fragmented_stream(data), BOUNDARY).read_part()

    def test_rejects_invalid_jpeg_markers(self) -> None:
        data = part(payload=b"not-a-jpeg")
        with self.assertRaisesRegex(ReceiverError, "invalid JPEG SOI/EOI"):
            MultipartReader(fragmented_stream(data), BOUNDARY).read_part()

    def test_rejects_missing_part_trailer(self) -> None:
        data = part()[:-2]
        with self.assertRaisesRegex(ReceiverError, "part trailer"):
            MultipartReader(fragmented_stream(data), BOUNDARY).read_part()


class ReceiverStatisticsTests(unittest.TestCase):
    @staticmethod
    def _part(frame_id: int) -> MjpegPart:
        return MjpegPart(
            frame_id=frame_id,
            capture_timestamp_us=frame_id * 1000,
            payload=JPEG,
        )

    def test_summarizes_ids_windows_interarrival_and_throughput(self) -> None:
        statistics = ReceiverStatistics(measurement_start=10.0, planned_duration=3.0)
        samples = [
            (10, 10.1),
            (11, 10.2),
            (13, 11.1),
            (13, 11.5),
            (12, 11.7),
            (15, 12.2),
        ]
        for frame_id, arrival_time in samples:
            self.assertTrue(statistics.record(self._part(frame_id), arrival_time))

        report = statistics.summarize(13.0, "duration_complete")
        self.assertEqual(report.frames_received, 6)
        self.assertEqual(report.duplicate_frame_ids, 1)
        self.assertEqual(report.missing_frame_ids, 1)
        self.assertEqual(report.out_of_order_frames, 1)
        self.assertAlmostEqual(report.average_fps, 2.0)
        self.assertEqual(report.one_second_window_fps, [2, 3, 1])
        self.assertEqual(report.min_1s_fps, 1)
        self.assertEqual(report.max_1s_fps, 3)
        self.assertAlmostEqual(report.average_interarrival_ms, 420.0)
        self.assertAlmostEqual(report.p50_interarrival_ms, 400.0)
        self.assertAlmostEqual(report.p95_interarrival_ms, 900.0)
        self.assertAlmostEqual(report.max_interarrival_ms, 900.0)
        self.assertEqual(report.interarrival_over_200ms, 3)
        self.assertAlmostEqual(report.relative_latency_drift_ms, 2095.0)
        self.assertAlmostEqual(report.max_relative_latency_growth_ms, 2095.0)
        self.assertAlmostEqual(report.average_jpeg_size, len(JPEG))
        self.assertEqual(report.bytes_received, 6 * len(JPEG))
        self.assertAlmostEqual(report.throughput_bps, 6 * len(JPEG) * 8 / 3)
        self.assertEqual(report.connection_drop_count, 0)
        self.assertIsNone(report.error)

    def test_excludes_warmup_and_deadline_arrivals(self) -> None:
        statistics = ReceiverStatistics(measurement_start=5.0, planned_duration=2.0)
        self.assertFalse(statistics.record(self._part(1), 4.999))
        self.assertTrue(statistics.record(self._part(2), 5.0))
        self.assertFalse(statistics.record(self._part(3), 7.0))

        report = statistics.summarize(8.0, "duration_complete")
        self.assertEqual(report.frames_received, 1)
        self.assertEqual(report.one_second_window_fps, [1, 0])
        self.assertAlmostEqual(report.elapsed_s, 2.0)

    def test_early_drop_uses_observed_time_and_complete_windows(self) -> None:
        statistics = ReceiverStatistics(measurement_start=1.0, planned_duration=10.0)
        statistics.record(self._part(1), 1.2)
        statistics.record_connection_drop()
        report = statistics.summarize(
            2.5,
            "connection_drop",
            error="timed out",
        )

        self.assertAlmostEqual(report.elapsed_s, 1.5)
        self.assertEqual(report.one_second_window_fps, [1])
        self.assertEqual(report.connection_drop_count, 1)
        self.assertEqual(report.error, "timed out")

    def test_empty_report_uses_finite_zero_values(self) -> None:
        report = ReceiverStatistics(0.0, 0.5).summarize(
            0.5, "duration_complete"
        )
        self.assertEqual(report.frames_received, 0)
        self.assertEqual(report.average_fps, 0.0)
        self.assertEqual(report.average_interarrival_ms, 0.0)
        self.assertEqual(report.p95_interarrival_ms, 0.0)
        self.assertEqual(report.interarrival_over_200ms, 0)
        self.assertEqual(report.relative_latency_drift_ms, 0.0)
        self.assertEqual(report.max_relative_latency_growth_ms, 0.0)
        self.assertEqual(report.average_jpeg_size, 0.0)
        self.assertEqual(report.throughput_bps, 0.0)
        self.assertEqual(report.one_second_window_fps, [])

    def test_terminal_and_json_reports_contain_step_21_fields(self) -> None:
        statistics = ReceiverStatistics(0.0, 1.0)
        statistics.record(self._part(7), 0.5)
        report = statistics.summarize(1.0, "duration_complete")

        terminal = format_terminal_summary(report)
        self.assertIn("frames_received=1", terminal)
        self.assertIn("p95_interarrival_ms=0.000", terminal)
        self.assertIn("interarrival_over_200ms=0", terminal)
        self.assertIn("relative_latency_drift_ms=0.000", terminal)
        self.assertIn("connection_drop_count=0", terminal)

        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "report.json"
            write_json_report(report, str(destination))
            serialized = json.loads(destination.read_text(encoding="utf-8"))
        self.assertEqual(serialized["frames_received"], 1)
        self.assertEqual(serialized["1_second_window_fps"], [1])
        self.assertEqual(serialized["interarrival_over_200ms"], 0)
        self.assertEqual(serialized["relative_latency_drift_ms"], 0.0)
        self.assertNotIn("one_second_window_fps", serialized)
        self.assertIsNone(serialized["error"])

    def test_stall_threshold_is_strictly_greater_than_200ms(self) -> None:
        statistics = ReceiverStatistics(0.0, 2.0)
        statistics.record(self._part(1), 0.1)
        statistics.record(self._part(2), 0.3)
        statistics.record(self._part(3), 0.500001)

        report = statistics.summarize(2.0, "duration_complete")
        self.assertEqual(report.interarrival_over_200ms, 1)

    def test_reports_relative_latency_growth_without_clock_sync(self) -> None:
        statistics = ReceiverStatistics(0.0, 2.0)
        samples = [
            MjpegPart(1, 10_000_000, JPEG),
            MjpegPart(2, 10_040_000, JPEG),
            MjpegPart(3, 10_080_000, JPEG),
        ]
        statistics.record(samples[0], 0.10)
        statistics.record(samples[1], 0.16)
        statistics.record(samples[2], 0.23)

        report = statistics.summarize(2.0, "duration_complete")
        self.assertAlmostEqual(report.relative_latency_drift_ms, 50.0)
        self.assertAlmostEqual(report.max_relative_latency_growth_ms, 50.0)


if __name__ == "__main__":
    unittest.main()
