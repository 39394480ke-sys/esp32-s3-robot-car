#!/usr/bin/env python3
"""Receive and structurally validate the D2 MJPEG stream without decoding it."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from email.message import Message
import http.client
import json
import math
from pathlib import Path
import socket
import sys
import time
from typing import BinaryIO, Optional
from urllib.parse import SplitResult, urlsplit


MAX_BOUNDARY_BYTES = 70
MAX_HEADER_LINE_BYTES = 1024
MAX_PART_HEADER_BYTES = 4096
MAX_PART_HEADERS = 32
DEFAULT_MAX_JPEG_BYTES = 2 * 1024 * 1024


class ReceiverError(Exception):
    """Raised when the HTTP or multipart stream violates the D2 contract."""


class MultipartEnd(Exception):
    """Raised when a multipart closing boundary is received."""


@dataclass(frozen=True)
class MjpegPart:
    frame_id: int
    capture_timestamp_us: int
    payload: bytes


@dataclass(frozen=True)
class ReceiverReport:
    elapsed_s: float
    frames_received: int
    duplicate_frame_ids: int
    missing_frame_ids: int
    out_of_order_frames: int
    average_fps: float
    one_second_window_fps: list[int]
    min_1s_fps: int
    max_1s_fps: int
    average_interarrival_ms: float
    p50_interarrival_ms: float
    p95_interarrival_ms: float
    max_interarrival_ms: float
    interarrival_over_200ms: int
    relative_latency_drift_ms: float
    max_relative_latency_growth_ms: float
    average_jpeg_size: float
    bytes_received: int
    throughput_bps: float
    connection_drop_count: int
    termination_reason: str
    error: Optional[str]


class ReceiverStatistics:
    def __init__(self, measurement_start: float, planned_duration: float) -> None:
        if planned_duration <= 0:
            raise ValueError("planned_duration must be positive")
        self.measurement_start = measurement_start
        self.planned_duration = planned_duration
        self.frames_received = 0
        self.bytes_received = 0
        self.duplicate_frame_ids = 0
        self.out_of_order_frames = 0
        self.connection_drop_count = 0
        self._arrival_times: list[float] = []
        self._capture_timestamps_us: list[int] = []
        self._jpeg_sizes: list[int] = []
        self._frame_ids: set[int] = set()
        self._lowest_frame_id: Optional[int] = None
        self._highest_frame_id: Optional[int] = None
        self._window_frames = [0] * int(math.floor(planned_duration))

    def record(self, part: MjpegPart, arrival_time: float) -> bool:
        offset = arrival_time - self.measurement_start
        if offset < 0 or offset >= self.planned_duration:
            return False

        self.frames_received += 1
        self.bytes_received += len(part.payload)
        self._arrival_times.append(arrival_time)
        self._capture_timestamps_us.append(part.capture_timestamp_us)
        self._jpeg_sizes.append(len(part.payload))
        window = int(offset)
        if window < len(self._window_frames):
            self._window_frames[window] += 1

        if part.frame_id in self._frame_ids:
            self.duplicate_frame_ids += 1
            return True
        if self._highest_frame_id is not None and part.frame_id < self._highest_frame_id:
            self.out_of_order_frames += 1
        self._frame_ids.add(part.frame_id)
        if self._lowest_frame_id is None or part.frame_id < self._lowest_frame_id:
            self._lowest_frame_id = part.frame_id
        if self._highest_frame_id is None or part.frame_id > self._highest_frame_id:
            self._highest_frame_id = part.frame_id
        return True

    def record_connection_drop(self) -> None:
        self.connection_drop_count += 1

    @staticmethod
    def _nearest_rank(values: list[float], percent: int) -> float:
        if not values:
            return 0.0
        ordered = sorted(values)
        rank = (percent * len(ordered) + 99) // 100
        return ordered[rank - 1]

    def summarize(
        self,
        measurement_end: float,
        termination_reason: str,
        error: Optional[str] = None,
    ) -> ReceiverReport:
        elapsed_s = max(
            0.0,
            min(
                measurement_end - self.measurement_start,
                self.planned_duration,
            ),
        )
        interarrival_ms = [
            (current - previous) * 1000.0
            for previous, current in zip(
                self._arrival_times,
                self._arrival_times[1:],
            )
        ]
        complete_window_count = min(
            len(self._window_frames), int(math.floor(elapsed_s))
        )
        windows = self._window_frames[:complete_window_count]

        missing_frame_ids = 0
        if self._lowest_frame_id is not None and self._highest_frame_id is not None:
            expected_unique = self._highest_frame_id - self._lowest_frame_id + 1
            missing_frame_ids = expected_unique - len(self._frame_ids)

        average_fps = self.frames_received / elapsed_s if elapsed_s > 0 else 0.0
        average_jpeg_size = (
            self.bytes_received / self.frames_received
            if self.frames_received > 0
            else 0.0
        )
        throughput_bps = (
            self.bytes_received * 8.0 / elapsed_s if elapsed_s > 0 else 0.0
        )
        average_interarrival_ms = (
            sum(interarrival_ms) / len(interarrival_ms)
            if interarrival_ms
            else 0.0
        )
        latency_growth_ms = [0.0]
        if len(self._arrival_times) >= 2:
            first_arrival = self._arrival_times[0]
            first_capture_us = self._capture_timestamps_us[0]
            latency_growth_ms = [
                ((arrival - first_arrival) * 1000.0)
                - ((capture_us - first_capture_us) / 1000.0)
                for arrival, capture_us in zip(
                    self._arrival_times,
                    self._capture_timestamps_us,
                )
            ]

        return ReceiverReport(
            elapsed_s=elapsed_s,
            frames_received=self.frames_received,
            duplicate_frame_ids=self.duplicate_frame_ids,
            missing_frame_ids=missing_frame_ids,
            out_of_order_frames=self.out_of_order_frames,
            average_fps=average_fps,
            one_second_window_fps=windows,
            min_1s_fps=min(windows) if windows else 0,
            max_1s_fps=max(windows) if windows else 0,
            average_interarrival_ms=average_interarrival_ms,
            p50_interarrival_ms=self._nearest_rank(interarrival_ms, 50),
            p95_interarrival_ms=self._nearest_rank(interarrival_ms, 95),
            max_interarrival_ms=max(interarrival_ms) if interarrival_ms else 0.0,
            interarrival_over_200ms=sum(
                interval > 200.0 for interval in interarrival_ms
            ),
            relative_latency_drift_ms=latency_growth_ms[-1],
            max_relative_latency_growth_ms=max(0.0, max(latency_growth_ms)),
            average_jpeg_size=average_jpeg_size,
            bytes_received=self.bytes_received,
            throughput_bps=throughput_bps,
            connection_drop_count=self.connection_drop_count,
            termination_reason=termination_reason,
            error=error,
        )


@dataclass(frozen=True)
class ReceiveOutcome:
    report: ReceiverReport
    error: Optional[str]


def extract_multipart_boundary(content_type: str) -> bytes:
    message = Message()
    message["Content-Type"] = content_type
    if message.get_content_type().lower() != "multipart/x-mixed-replace":
        raise ReceiverError(
            "expected multipart/x-mixed-replace response, "
            f"got {content_type!r}"
        )

    boundary = message.get_param("boundary", header="content-type")
    if not isinstance(boundary, str) or not boundary:
        raise ReceiverError("multipart response is missing its boundary")
    try:
        encoded = boundary.encode("ascii")
    except UnicodeEncodeError as error:
        raise ReceiverError("multipart boundary must be ASCII") from error
    if len(encoded) > MAX_BOUNDARY_BYTES or b"\r" in encoded or b"\n" in encoded:
        raise ReceiverError("multipart boundary is invalid or too long")
    return encoded


def _parse_non_negative_integer(value: str, name: str) -> int:
    if not value or not value.isascii() or not value.isdecimal():
        raise ReceiverError(f"{name} must be a non-negative decimal integer")
    return int(value, 10)


class MultipartReader:
    def __init__(
        self,
        source: BinaryIO,
        boundary: bytes,
        max_jpeg_bytes: int = DEFAULT_MAX_JPEG_BYTES,
    ) -> None:
        if not boundary:
            raise ValueError("boundary must not be empty")
        if max_jpeg_bytes <= 0:
            raise ValueError("max_jpeg_bytes must be positive")
        self._source = source
        self._boundary_line = b"--" + boundary
        self._max_jpeg_bytes = max_jpeg_bytes

    def _readline(self, context: str) -> bytes:
        line = self._source.readline(MAX_HEADER_LINE_BYTES + 1)
        if not line:
            raise ReceiverError(f"stream ended while reading {context}")
        if len(line) > MAX_HEADER_LINE_BYTES:
            raise ReceiverError(f"{context} exceeds the line-size limit")
        if not line.endswith(b"\r\n"):
            raise ReceiverError(f"{context} is not CRLF terminated")
        return line

    def _read_exact(self, length: int, context: str) -> bytes:
        chunks = []
        remaining = length
        while remaining:
            chunk = self._source.read(remaining)
            if not chunk:
                received = length - remaining
                raise ReceiverError(
                    f"stream ended during {context}: expected {length} bytes, "
                    f"received {received}"
                )
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _read_headers(self) -> dict[str, str]:
        headers: dict[str, str] = {}
        total_bytes = 0
        for _ in range(MAX_PART_HEADERS):
            line = self._readline("part header")
            total_bytes += len(line)
            if total_bytes > MAX_PART_HEADER_BYTES:
                raise ReceiverError("part headers exceed the total-size limit")
            if line == b"\r\n":
                return headers
            try:
                name_bytes, value_bytes = line[:-2].split(b":", 1)
                name = name_bytes.decode("ascii").strip().lower()
                value = value_bytes.decode("ascii").strip()
            except (ValueError, UnicodeDecodeError) as error:
                raise ReceiverError("part header is malformed or non-ASCII") from error
            if not name or name in headers:
                raise ReceiverError(f"duplicate or empty part header: {name!r}")
            headers[name] = value
        raise ReceiverError("part contains too many headers")

    def read_part(self) -> MjpegPart:
        boundary_line = self._readline("multipart boundary")[:-2]
        if boundary_line == self._boundary_line + b"--":
            raise MultipartEnd
        if boundary_line != self._boundary_line:
            raise ReceiverError(
                f"unexpected multipart boundary line: {boundary_line!r}"
            )

        headers = self._read_headers()
        content_type = headers.get("content-type", "")
        if content_type.split(";", 1)[0].strip().lower() != "image/jpeg":
            raise ReceiverError("part Content-Type must be image/jpeg")

        content_length = _parse_non_negative_integer(
            headers.get("content-length", ""), "Content-Length"
        )
        if content_length == 0 or content_length > self._max_jpeg_bytes:
            raise ReceiverError(
                f"Content-Length {content_length} is outside the accepted range"
            )
        frame_id = _parse_non_negative_integer(
            headers.get("x-frame-id", ""), "X-Frame-Id"
        )
        capture_timestamp_us = _parse_non_negative_integer(
            headers.get("x-capture-timestamp-us", ""),
            "X-Capture-Timestamp-Us",
        )

        payload = self._read_exact(content_length, "JPEG payload")
        if (
            len(payload) < 4
            or not payload.startswith(b"\xff\xd8")
            or not payload.endswith(b"\xff\xd9")
        ):
            raise ReceiverError(f"frame {frame_id} has invalid JPEG SOI/EOI markers")
        if self._read_exact(2, "part trailer") != b"\r\n":
            raise ReceiverError(f"frame {frame_id} is missing its trailing CRLF")

        return MjpegPart(
            frame_id=frame_id,
            capture_timestamp_us=capture_timestamp_us,
            payload=payload,
        )


def _validate_url(url: str) -> SplitResult:
    parsed = urlsplit(url)
    if parsed.scheme not in {"http", "https"}:
        raise ReceiverError("stream URL must use http or https")
    if not parsed.hostname:
        raise ReceiverError("stream URL must include a host")
    if parsed.username is not None or parsed.password is not None:
        raise ReceiverError("credentials in the stream URL are not supported")
    if parsed.fragment:
        raise ReceiverError("stream URL must not contain a fragment")
    return parsed


def _request_target(parsed: SplitResult) -> str:
    target = parsed.path or "/"
    if parsed.query:
        target += "?" + parsed.query
    return target


def receive_stream(
    url: str,
    duration_seconds: float,
    warmup_seconds: float,
    connect_timeout_seconds: float,
    read_timeout_seconds: float,
    max_jpeg_bytes: int,
    progress_interval_seconds: float = 5.0,
) -> ReceiveOutcome:
    parsed = _validate_url(url)
    connection_type = (
        http.client.HTTPSConnection
        if parsed.scheme == "https"
        else http.client.HTTPConnection
    )
    connection = connection_type(
        parsed.hostname,
        parsed.port,
        timeout=connect_timeout_seconds,
    )
    response: Optional[http.client.HTTPResponse] = None

    try:
        connection.connect()
        if connection.sock is not None:
            connection.sock.settimeout(read_timeout_seconds)
        connection.request(
            "GET",
            _request_target(parsed),
            headers={
                "Accept": "multipart/x-mixed-replace",
                "Cache-Control": "no-cache",
                "Connection": "close",
                "User-Agent": "d2-pc-benchmark-receiver/1",
            },
        )
        response = connection.getresponse()
        if response.status != http.client.OK:
            raise ReceiverError(
                f"stream request returned HTTP {response.status} {response.reason}"
            )
        boundary = extract_multipart_boundary(response.getheader("Content-Type", ""))
    except (OSError, socket.timeout, http.client.HTTPException) as error:
        if response is not None:
            response.close()
        connection.close()
        raise ReceiverError(f"stream connection failed: {error}") from error
    except ReceiverError:
        if response is not None:
            response.close()
        connection.close()
        raise

    stream_started_at = time.monotonic()
    measurement_start = stream_started_at + warmup_seconds
    deadline = measurement_start + duration_seconds
    statistics = ReceiverStatistics(measurement_start, duration_seconds)
    reader = MultipartReader(response, boundary, max_jpeg_bytes)
    next_progress_at = measurement_start + progress_interval_seconds
    termination_reason = "duration_complete"
    receive_error: Optional[str] = None

    print(
        "receiver connected "
        f"status={response.status} boundary={boundary.decode('ascii')} "
        f"warmup_s={warmup_seconds:.3f} duration_s={duration_seconds:.3f}"
    )

    try:
        while time.monotonic() < deadline:
            try:
                part = reader.read_part()
            except MultipartEnd:
                statistics.record_connection_drop()
                termination_reason = "multipart_ended"
                receive_error = "multipart stream ended before the measurement deadline"
                break

            arrival_time = time.monotonic()
            statistics.record(part, arrival_time)
            if arrival_time >= measurement_start and arrival_time >= next_progress_at:
                print(
                    "receiver progress "
                    f"elapsed_s={min(arrival_time - measurement_start, duration_seconds):.3f} "
                    f"frames_received={statistics.frames_received} "
                    f"bytes_received={statistics.bytes_received}"
                )
                next_progress_at = arrival_time + progress_interval_seconds
    except (ReceiverError, OSError, socket.timeout, http.client.HTTPException) as error:
        statistics.record_connection_drop()
        termination_reason = "connection_drop"
        receive_error = str(error)
    finally:
        response.close()
        connection.close()

    finished_at = min(time.monotonic(), deadline)
    report = statistics.summarize(
        finished_at,
        termination_reason=termination_reason,
        error=receive_error,
    )
    return ReceiveOutcome(report=report, error=receive_error)


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def positive_integer(value: str) -> int:
    parsed = int(value, 10)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def non_negative_float(value: str) -> float:
    parsed = float(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("value must be non-negative")
    return parsed


def format_terminal_summary(report: ReceiverReport) -> str:
    windows = ",".join(str(value) for value in report.one_second_window_fps)
    return "\n".join(
        [
            "D2 PC Receiver Summary",
            f"termination_reason={report.termination_reason}",
            f"elapsed_s={report.elapsed_s:.3f}",
            f"frames_received={report.frames_received}",
            f"duplicate_frame_ids={report.duplicate_frame_ids}",
            f"missing_frame_ids={report.missing_frame_ids}",
            f"out_of_order_frames={report.out_of_order_frames}",
            f"average_fps={report.average_fps:.3f}",
            f"1_second_window_fps=[{windows}]",
            f"min_1s_fps={report.min_1s_fps}",
            f"max_1s_fps={report.max_1s_fps}",
            f"average_interarrival_ms={report.average_interarrival_ms:.3f}",
            f"p50_interarrival_ms={report.p50_interarrival_ms:.3f}",
            f"p95_interarrival_ms={report.p95_interarrival_ms:.3f}",
            f"max_interarrival_ms={report.max_interarrival_ms:.3f}",
            f"interarrival_over_200ms={report.interarrival_over_200ms}",
            f"relative_latency_drift_ms={report.relative_latency_drift_ms:.3f}",
            "max_relative_latency_growth_ms="
            f"{report.max_relative_latency_growth_ms:.3f}",
            f"average_jpeg_size={report.average_jpeg_size:.3f}",
            f"bytes_received={report.bytes_received}",
            f"throughput_bps={report.throughput_bps:.3f}",
            f"connection_drop_count={report.connection_drop_count}",
            f"error={report.error if report.error is not None else 'null'}",
        ]
    )


def write_json_report(report: ReceiverReport, destination: str) -> None:
    report_data = asdict(report)
    report_data["1_second_window_fps"] = report_data.pop(
        "one_second_window_fps"
    )
    serialized = json.dumps(report_data, indent=2, sort_keys=True) + "\n"
    if destination == "-":
        print(serialized, end="")
        return
    Path(destination).write_text(serialized, encoding="utf-8")


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Receive D2 multipart MJPEG without decoding or display."
    )
    parser.add_argument("--url", required=True, help="Robot /stream URL")
    parser.add_argument(
        "--duration",
        type=positive_float,
        default=15.0,
        help="receive duration in seconds (default: 15)",
    )
    parser.add_argument(
        "--warmup-seconds",
        type=non_negative_float,
        default=0.0,
        help="parse but do not count this initial period (default: 0)",
    )
    parser.add_argument(
        "--connect-timeout",
        type=positive_float,
        default=5.0,
        help="TCP/HTTP connection timeout in seconds (default: 5)",
    )
    parser.add_argument(
        "--read-timeout",
        type=positive_float,
        default=15.0,
        help="individual network read timeout in seconds (default: 15)",
    )
    parser.add_argument(
        "--max-jpeg-bytes",
        type=positive_integer,
        default=DEFAULT_MAX_JPEG_BYTES,
        help="maximum accepted Content-Length (default: 2097152)",
    )
    parser.add_argument(
        "--json-out",
        help="write the final report as JSON; use - for stdout",
    )
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    arguments = build_argument_parser().parse_args(argv)
    try:
        outcome = receive_stream(
            url=arguments.url,
            duration_seconds=arguments.duration,
            warmup_seconds=arguments.warmup_seconds,
            connect_timeout_seconds=arguments.connect_timeout,
            read_timeout_seconds=arguments.read_timeout,
            max_jpeg_bytes=arguments.max_jpeg_bytes,
        )
    except KeyboardInterrupt:
        print("receiver stopped by user", file=sys.stderr)
        return 130
    except ReceiverError as error:
        print(f"receiver error: {error}", file=sys.stderr)
        return 1

    print(format_terminal_summary(outcome.report))
    if arguments.json_out:
        try:
            write_json_report(outcome.report, arguments.json_out)
        except OSError as error:
            print(f"receiver error: unable to write JSON report: {error}", file=sys.stderr)
            return 1
    if outcome.error is not None:
        print(f"receiver error: {outcome.error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
