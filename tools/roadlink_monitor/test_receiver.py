import json
import queue
import tempfile
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from roadlink_monitor import ServerController, TelemetryReceiver


class ReceiverTest(unittest.TestCase):
    def test_accepts_roadlink_json_and_logs_it(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "telemetry.jsonl"
            receiver = TelemetryReceiver("123456", log_path)
            server = ServerController("127.0.0.1", 0, receiver)
            server.start()
            try:
                payload = {
                    "access_key": "123456",
                    "device": "roadlink",
                    "gps": {
                        "valid": True,
                        "latitude": 34.1,
                        "longitude": -118.2,
                    },
                    "obd": {"rpm": 1800, "speed_kmh": 42},
                }
                request = urllib.request.Request(
                    f"http://127.0.0.1:{server.port}/telemetry",
                    data=json.dumps(payload).encode("utf-8"),
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(request, timeout=2) as response:
                    self.assertEqual(response.status, 200)

                event = receiver.events.get(timeout=2)
                self.assertEqual(event["device"], "roadlink")
                self.assertEqual(event["obd"]["rpm"], 1800)
                self.assertTrue(log_path.exists())
                self.assertEqual(
                    json.loads(log_path.read_text(encoding="utf-8"))["gps"]["valid"],
                    True,
                )
            finally:
                server.stop()

    def test_rejects_invalid_json(self) -> None:
        receiver = TelemetryReceiver("123456")
        server = ServerController("127.0.0.1", 0, receiver)
        server.start()
        try:
            request = urllib.request.Request(
                f"http://127.0.0.1:{server.port}/telemetry",
                data=b"not-json",
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(request, timeout=2)
            self.assertEqual(error.exception.code, 400)
            with self.assertRaises(queue.Empty):
                receiver.events.get_nowait()
        finally:
            server.stop()

    def test_rejects_wrong_access_key(self) -> None:
        receiver = TelemetryReceiver("123456")
        server = ServerController("127.0.0.1", 0, receiver)
        server.start()
        try:
            request = urllib.request.Request(
                f"http://127.0.0.1:{server.port}/telemetry",
                data=json.dumps(
                    {"access_key": "999999", "device": "roadlink"}
                ).encode("utf-8"),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(request, timeout=2)
            self.assertEqual(error.exception.code, 401)
            with self.assertRaises(queue.Empty):
                receiver.events.get_nowait()
        finally:
            server.stop()


if __name__ == "__main__":
    unittest.main()
