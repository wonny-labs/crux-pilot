#!/usr/bin/env python3
"""Size the header capture against Firefox, over Marionette, with the image already running.

Firefox takes the probe's cookies, then loads a page whose own script makes the requests a client makes.
The image reports only the length of what it captured. A manual measurement, because a browser is not in the images CI uses.

    firefox_capture.py <firefox binary>
"""
import json
import socket
import subprocess
import sys
import tempfile
import time

BASE = "http://localhost:18184"
MARIONETTE = 2828
READ = "return document.getElementById('out').textContent"


class Marionette:
    def __init__(self):
        self.sock = socket.create_connection(("127.0.0.1", MARIONETTE), timeout=60)
        self.buffer = b""
        self.next_id = 1
        self.read()

    def read(self):
        while b":" not in self.buffer:
            self.buffer += self.sock.recv(65536)
        size, rest = self.buffer.split(b":", 1)
        while len(rest) < int(size):
            rest += self.sock.recv(65536)
        message, self.buffer = rest[:int(size)], rest[int(size):]
        return json.loads(message)

    def call(self, command, params):
        body = json.dumps([0, self.next_id, command, params]).encode()
        self.next_id += 1
        self.sock.sendall(str(len(body)).encode() + b":" + body)
        _, _, error, result = self.read()
        if error:
            raise RuntimeError(f"{command}: {error}")
        return result


def main():
    with tempfile.TemporaryDirectory() as profile:
        browser = subprocess.Popen([sys.argv[1], "--headless", "--marionette", "--no-remote", "--profile", profile],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(240):
                try:
                    client = Marionette()
                    break
                except OSError:
                    time.sleep(1)
            else:
                raise SystemExit("Marionette did not start")
            client.call("WebDriver:NewSession", {"capabilities": {"alwaysMatch": {"acceptInsecureCerts": True}}})
            client.call("WebDriver:Navigate", {"url": f"{BASE}/probe/cookies"})
            client.call("WebDriver:Navigate", {"url": f"{BASE}/probe/page"})
            text = ""
            for _ in range(120):
                text = client.call("WebDriver:ExecuteScript", {"script": READ, "args": []})["value"]
                if text:
                    break
                time.sleep(0.5)
            result = json.loads(text)
            print(json.dumps(result, indent=2, sort_keys=True))
            if "error" in result:
                raise SystemExit(result["error"])
            lengths = result["post"]
            if result["postStatus"] != 200 or lengths["cookie"] < 100 or lengths["request"] != 1:
                raise SystemExit("the capture did not hold what Firefox sent")
        finally:
            browser.kill()
            browser.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
