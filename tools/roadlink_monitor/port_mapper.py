"""Temporary NAT-PMP/UPnP TCP port mapping for RoadLink Monitor."""

from __future__ import annotations

import ipaddress
import random
import re
import socket
import struct
import subprocess
import threading
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from dataclasses import dataclass


LEASE_SECONDS = 3600


@dataclass
class MappingResult:
    protocol: str
    public_ip: str
    public_port: int
    lifetime: int


def default_gateway() -> str:
    output = subprocess.check_output(
        ["route", "print", "-4", "0.0.0.0"],
        text=True,
        errors="replace",
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    candidates: list[tuple[int, str]] = []
    pattern = re.compile(
        r"^\s*0\.0\.0\.0\s+0\.0\.0\.0\s+(\d+\.\d+\.\d+\.\d+)"
        r"\s+\d+\.\d+\.\d+\.\d+\s+(\d+)\s*$"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            candidates.append((int(match.group(2)), match.group(1)))
    if not candidates:
        raise RuntimeError("Windows default gateway was not found")
    return min(candidates)[1]


def local_ip_for(remote_ip: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect((remote_ip, 9))
        return str(probe.getsockname()[0])


class NatPmpMapping:
    protocol = "NAT-PMP"

    def __init__(self, gateway: str, internal_port: int) -> None:
        self.gateway = gateway
        self.internal_port = internal_port
        self.public_port = 0

    def _request(self, packet: bytes, expected_opcode: int) -> bytes:
        delay = 0.25
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(delay)
            for _ in range(6):
                sock.sendto(packet, (self.gateway, 5351))
                try:
                    response, address = sock.recvfrom(64)
                    if address[0] == self.gateway and len(response) >= 8:
                        version, opcode, result = struct.unpack("!BBH", response[:4])
                        if version != 0 or opcode != expected_opcode + 128:
                            continue
                        if result:
                            raise RuntimeError(f"NAT-PMP gateway returned error {result}")
                        return response
                except socket.timeout:
                    delay = min(delay * 2, 2.0)
                    sock.settimeout(delay)
        raise RuntimeError("NAT-PMP gateway did not respond")

    def open(self, requested_port: int, lifetime: int = LEASE_SECONDS) -> MappingResult:
        address = self._request(b"\x00\x00", 0)
        public_ip = socket.inet_ntoa(address[8:12])
        packet = struct.pack(
            "!BBHHHI", 0, 2, 0, self.internal_port, requested_port, lifetime
        )
        response = self._request(packet, 2)
        _, self.public_port, granted = struct.unpack("!HHI", response[8:16])
        return MappingResult(self.protocol, public_ip, self.public_port, granted)

    def renew(self, lifetime: int = LEASE_SECONDS) -> MappingResult:
        return self.open(self.public_port, lifetime)

    def close(self) -> None:
        if not self.public_port:
            return
        packet = struct.pack(
            "!BBHHHI", 0, 2, 0, self.internal_port, self.public_port, 0
        )
        try:
            self._request(packet, 2)
        except Exception:
            pass


class UpnpMapping:
    protocol = "UPnP IGD"

    def __init__(self, internal_port: int) -> None:
        self.internal_port = internal_port
        self.public_port = 0
        self.control_url = ""
        self.service_type = ""
        self.local_ip = ""
        self.public_ip = ""

    @staticmethod
    def _discover() -> str:
        message = (
            "M-SEARCH * HTTP/1.1\r\n"
            "HOST: 239.255.255.250:1900\r\n"
            'MAN: "ssdp:discover"\r\n'
            "MX: 2\r\n"
            "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n"
        ).encode("ascii")
        locations: set[str] = set()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(2.5)
            sock.sendto(message, ("239.255.255.250", 1900))
            while True:
                try:
                    data, _ = sock.recvfrom(65535)
                except socket.timeout:
                    break
                text = data.decode("iso-8859-1", errors="replace")
                match = re.search(r"(?im)^location:\s*(\S+)\s*$", text)
                if match:
                    locations.add(match.group(1))
        if not locations:
            raise RuntimeError("No UPnP internet gateway responded")
        return next(iter(locations))

    def _prepare(self) -> None:
        location = self._discover()
        with urllib.request.urlopen(location, timeout=4) as response:
            root = ET.fromstring(response.read())
        for service in root.iter():
            if not service.tag.endswith("service"):
                continue
            values = {
                child.tag.rsplit("}", 1)[-1]: (child.text or "")
                for child in service
            }
            service_type = values.get("serviceType", "")
            if "WANIPConnection" in service_type or "WANPPPConnection" in service_type:
                self.service_type = service_type
                self.control_url = urllib.parse.urljoin(
                    location, values.get("controlURL", "")
                )
                self.local_ip = local_ip_for(urllib.parse.urlparse(location).hostname or "")
                return
        raise RuntimeError("UPnP gateway has no WAN connection service")

    def _soap(self, action: str, arguments: dict[str, str]) -> ET.Element:
        body_values = "".join(
            f"<New{name}>{value}</New{name}>" for name, value in arguments.items()
        )
        envelope = (
            '<?xml version="1.0"?>'
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
            's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
            f'<s:Body><u:{action} xmlns:u="{self.service_type}">'
            f"{body_values}</u:{action}></s:Body></s:Envelope>"
        ).encode("utf-8")
        request = urllib.request.Request(
            self.control_url,
            data=envelope,
            headers={
                "Content-Type": 'text/xml; charset="utf-8"',
                "SOAPAction": f'"{self.service_type}#{action}"',
            },
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=5) as response:
            return ET.fromstring(response.read())

    def open(self, requested_port: int, lifetime: int = LEASE_SECONDS) -> MappingResult:
        if not self.control_url:
            self._prepare()
        public = self._soap("GetExternalIPAddress", {})
        self.public_ip = next(
            (node.text or "" for node in public.iter()
             if node.tag.endswith("NewExternalIPAddress")),
            "",
        )
        last_error: Exception | None = None
        for candidate in [requested_port] + [
            random.randint(40000, 60000) for _ in range(7)
        ]:
            try:
                self._soap(
                    "AddPortMapping",
                    {
                        "RemoteHost": "",
                        "ExternalPort": str(candidate),
                        "Protocol": "TCP",
                        "InternalPort": str(self.internal_port),
                        "InternalClient": self.local_ip,
                        "Enabled": "1",
                        "PortMappingDescription": "RoadLink Monitor",
                        "LeaseDuration": str(lifetime),
                    },
                )
                self.public_port = candidate
                return MappingResult(
                    self.protocol, self.public_ip, candidate, lifetime
                )
            except Exception as error:
                last_error = error
        raise RuntimeError(f"UPnP could not add a temporary mapping: {last_error}")

    def renew(self, lifetime: int = LEASE_SECONDS) -> MappingResult:
        return self.open(self.public_port, lifetime)

    def close(self) -> None:
        if not self.public_port:
            return
        try:
            self._soap(
                "DeletePortMapping",
                {
                    "RemoteHost": "",
                    "ExternalPort": str(self.public_port),
                    "Protocol": "TCP",
                },
            )
        except Exception:
            pass


class AutoPortMapper:
    def __init__(self, internal_port: int, status_callback) -> None:
        self.internal_port = internal_port
        self.status_callback = status_callback
        self.mapping: NatPmpMapping | UpnpMapping | None = None
        self.result: MappingResult | None = None
        self.error = ""
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._thread = threading.Thread(
            target=self._run, name="roadlink-port-map", daemon=True
        )
        self._thread.start()

    def _run(self) -> None:
        self.status_callback("Discovering router and requesting temporary port...")
        requested_port = random.randint(40000, 60000)
        errors: list[str] = []
        try:
            gateway = default_gateway()
            candidates = [NatPmpMapping(gateway, self.internal_port),
                          UpnpMapping(self.internal_port)]
            for candidate in candidates:
                try:
                    result = candidate.open(requested_port)
                    if not ipaddress.ip_address(result.public_ip).is_global:
                        candidate.close()
                        raise RuntimeError(
                            f"router returned non-public address {result.public_ip} "
                            "(likely CGNAT)"
                        )
                    self.mapping = candidate
                    self.result = result
                    self.status_callback(
                        f"Public endpoint ready via {result.protocol}"
                    )
                    break
                except Exception as error:
                    errors.append(f"{candidate.protocol}: {error}")
            if self.mapping is None:
                raise RuntimeError("; ".join(errors))

            while not self._stop.wait(max(60, self.result.lifetime // 2)):
                self.result = self.mapping.renew()
                self.status_callback(
                    f"Public endpoint renewed via {self.result.protocol}"
                )
        except Exception as error:
            self.error = str(error)
            self.status_callback(f"Automatic public endpoint unavailable: {error}")
        finally:
            if self.mapping is not None:
                self.mapping.close()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=8)
