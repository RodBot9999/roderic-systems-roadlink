import importlib.util
import pathlib
import sys
import unittest


MODULE_PATH = pathlib.Path(__file__).parent / "virtual_roadlink.py"
SPEC = importlib.util.spec_from_file_location("virtual_roadlink", MODULE_PATH)
virtual_roadlink = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
sys.modules[SPEC.name] = virtual_roadlink
SPEC.loader.exec_module(virtual_roadlink)


class VirtualRoadLinkTests(unittest.TestCase):
    def config(self):
        return {
            "host": "127.0.0.1",
            "port": 8080,
            "access_key": "123456",
            "device_id": "RL-VIRTUAL-TEST",
            "imei": "867997069990099",
            "interval": 1.0,
        }

    def test_payload_matches_firmware_contract_and_has_unique_extension(self):
        device = virtual_roadlink.VirtualRoadLink(self.config())
        payload = device.payload()
        self.assertEqual(payload["access_key"], "123456")
        self.assertEqual(payload["device"], "roadlink")
        self.assertEqual(payload["device_id"], "RL-VIRTUAL-TEST")
        self.assertIn("latitude", payload["gps"])
        self.assertIn("rpm", payload["obd"])
        self.assertEqual(payload["sequence"], 1)

    def test_route_moves_smoothly_through_guadalajara(self):
        drive = virtual_roadlink.GuadalajaraDrive()
        first = (drive.state.latitude, drive.state.longitude)
        for _ in range(20):
            drive.step(1.0)
        second = (drive.state.latitude, drive.state.longitude)
        distance = virtual_roadlink.haversine_m(first, second)
        self.assertGreater(distance, 20)
        self.assertLess(distance, 500)
        self.assertGreater(drive.state.rpm, 700)
        self.assertGreater(drive.state.voltage_v, 13.5)

    def test_configuration_requires_exactly_six_digits(self):
        config = self.config()
        config["access_key"] = "12345"
        with self.assertRaises(ValueError):
            virtual_roadlink.validate_config(config)


if __name__ == "__main__":
    unittest.main()
