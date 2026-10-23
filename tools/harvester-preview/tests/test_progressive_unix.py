"""Linux IPC/lifetime checks using separate real reader processes."""
import copy
import json
import multiprocessing
import os
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

from scripts.progressive_session import Session, atomic_json, read_json
from scripts.progressive_unix import Provider, process_identity, socket_address, validate_snapshot


def reader(address, pipe):
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.connect(address)
    stream = connection.makefile("rb")
    heartbeat = json.loads(stream.readline())
    message = dict(heartbeat, pid=os.getpid(), type="attached")
    connection.sendall((json.dumps(message) + "\n").encode())
    pipe.send(os.getpid())
    command = pipe.recv()
    if command == "release":
        message["type"] = "released"
        connection.sendall((json.dumps(message) + "\n").encode())
    stream.close()
    connection.close()
    pipe.send("disconnected")
    # Stay alive after dropping IPC, allowing the lease policy to be checked.
    pipe.recv()


@unittest.skipUnless(os.name == "posix" and Path("/proc/self/stat").exists(), "Linux /proc required")
class UnixProviderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.session = Session(self.root, ["video.mp4.part", "audio.m4a.part"], [100, 100])
        for track in self.session.document["tracks"].values():
            (self.root / track["path"]).write_bytes(b"x" * 100)
        self.children = []
        self.addCleanup(self.stop_children)

    def wait(self, predicate, timeout=4):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if predicate():
                return
            time.sleep(.02)
        self.fail("Condition did not become true")

    def start_provider(self, owner=0):
        provider = Provider(self.session.path, owner, poll_interval=.02,
                            cleanup_delay=.12, unopened_delay=5)
        thread = threading.Thread(target=provider.run)
        thread.start()
        self.assertTrue(provider.ready.wait(3))
        self.assertIsNone(provider.error)
        def stop():
            provider.stop.set()
            thread.join(3)
            self.assertFalse(thread.is_alive())
        self.addCleanup(stop)
        return provider, thread

    def new_reader(self):
        context = multiprocessing.get_context("spawn")
        parent, child = context.Pipe()
        process = context.Process(target=reader, args=(socket_address(self.session.id), child))
        process.start()
        self.children.append((process, parent))
        self.assertTrue(parent.poll(3))
        pid = parent.recv()
        self.assertEqual(pid, process.pid)
        return process, parent

    def stop_children(self):
        for process, pipe in self.children:
            if process.is_alive():
                process.terminate()
            process.join(3)
            pipe.close()

    def complete(self):
        for track in self.session.document["tracks"].values():
            track.update(availablePrefixBytes=100, state="complete")
        self.session.state("complete")

    def test_process_identity_changes_after_exit(self):
        self.assertGreater(process_identity(os.getpid()), 0)
        process = subprocess.Popen(["sleep", "30"])
        try:
            self.assertGreater(process_identity(process.pid), 0)
        finally:
            process.terminate()
            process.wait()
        self.assertEqual(process_identity(process.pid), 0)

    def test_two_readers_release_and_crash_keep_sources_until_last_exit(self):
        provider, thread = self.start_provider()
        first, first_pipe = self.new_reader()
        second, second_pipe = self.new_reader()
        self.wait(lambda: len(provider.leases.items) == 2)
        self.complete()
        first_pipe.send("release")
        self.assertTrue(first_pipe.poll(3))
        self.assertEqual(first_pipe.recv(), "disconnected")
        self.wait(lambda: len(provider.leases.items) == 1)
        time.sleep(.2)
        self.assertTrue((self.root / "video.mp4.part").exists())
        second_pipe.send("disconnect")
        self.assertTrue(second_pipe.poll(3))
        self.assertEqual(second_pipe.recv(), "disconnected")
        time.sleep(.2)
        self.assertEqual(len(provider.leases.items), 1)
        self.assertTrue((self.root / "audio.m4a.part").exists())
        second.terminate()
        second.join(3)
        self.wait(lambda: not thread.is_alive())
        self.assertIsNone(provider.error)
        self.assertFalse((self.root / "video.mp4.part").exists())
        self.assertFalse((self.root / "audio.m4a.part").exists())

    def test_writer_exit_is_failure_and_keeps_diagnostics(self):
        writer = subprocess.Popen(["sleep", "30"])
        try:
            provider, thread = self.start_provider(writer.pid)
        finally:
            writer.terminate()
            writer.wait()
        self.wait(lambda: not thread.is_alive())
        self.assertEqual(read_json(self.session.path)["state"], "failed")
        self.assertTrue((self.root / "video.mp4.part").exists())

    def test_paused_writer_keeps_heartbeat_without_false_completion(self):
        provider, thread = self.start_provider()
        self.session.state("paused")
        self.wait(lambda: provider.state["state"] == "paused")
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(socket_address(self.session.id))
            connection.settimeout(3)
            stream = connection.makefile("rb")
            heartbeat = json.loads(stream.readline())
            self.assertEqual(heartbeat["type"], "heartbeat")
            time.sleep(1.1)
            self.assertEqual(json.loads(stream.readline())["type"], "heartbeat")
            stream.close()
        self.assertTrue(thread.is_alive())
        self.assertEqual(provider.state["state"], "paused")

    def test_spoofed_pid_rejected_without_lease(self):
        provider, _ = self.start_provider()
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(socket_address(self.session.id))
            connection.settimeout(3)
            heartbeat = json.loads(connection.recv(4096))
            connection.sendall((json.dumps(dict(heartbeat, type="attached", pid=os.getpid() + 1)) + "\n").encode())
            self.assertEqual(connection.recv(4096), b"")
        self.assertEqual(provider.leases.items, {})

    def test_two_readers_in_one_process_release_independently(self):
        provider, thread = self.start_provider()
        connections = []
        messages = []
        try:
            for number in (1, 2):
                connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                connections.append(connection)
                connection.connect(socket_address(self.session.id))
                connection.settimeout(3)
                heartbeat = json.loads(connection.recv(4096))
                message = dict(heartbeat, type="attached", pid=os.getpid(), readerId=f"{number:032x}")
                messages.append(message)
                connection.sendall((json.dumps(message) + "\n").encode())
            self.wait(lambda: len(provider.readers.get(str(os.getpid()), set())) == 2)
            self.complete()
            messages[0]["type"] = "released"
            connections[0].sendall((json.dumps(messages[0]) + "\n").encode())
            self.wait(lambda: len(provider.readers.get(str(os.getpid()), set())) == 1)
            time.sleep(.2)
            self.assertTrue((self.root / "video.mp4.part").exists())
            messages[1]["type"] = "released"
            connections[1].sendall((json.dumps(messages[1]) + "\n").encode())
            self.wait(lambda: not thread.is_alive())
            self.assertFalse((self.root / "video.mp4.part").exists())
        finally:
            for connection in connections:
                connection.close()

    def test_duplicate_message_fields_are_rejected(self):
        provider, _ = self.start_provider()
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(socket_address(self.session.id))
            connection.settimeout(3)
            connection.recv(4096)
            message = dict(schemaVersion=1, sessionId=self.session.id, generation=1,
                           revision=1, pid=os.getpid(), type="attached")
            raw = json.dumps(message)[:-1] + ', "pid": ' + str(os.getpid()) + '}\n'
            connection.sendall(raw.encode())
            self.assertEqual(connection.recv(4096), b"")
        self.assertEqual(provider.leases.items, {})

    def test_attachment_acknowledges_fresh_revision_after_persisting_lease(self):
        provider, _ = self.start_provider()
        self.session.publish()
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(socket_address(self.session.id))
            connection.settimeout(3)
            stream = connection.makefile("rb")
            json.loads(stream.readline())
            message = {k: self.session.document[k] for k in ("schemaVersion", "sessionId", "generation", "revision")}
            message.update(type="attached", pid=os.getpid(), readerId="a" * 32, leaseAck=True)
            connection.sendall((json.dumps(message) + "\n").encode())
            response = json.loads(stream.readline())
            self.assertEqual(response["type"], "attached")
            self.assertEqual(response["revision"], self.session.document["revision"])
            self.assertEqual(response["readerId"], "a" * 32)
            self.assertIn(str(os.getpid()), read_json(self.root / "leases.json")["leases"])
            message["type"] = "released"
            connection.sendall((json.dumps(message) + "\n").encode())
            self.wait(lambda: not provider.leases.items)
            stream.close()

    def test_prefix_regression_stops_provider_and_preserves_sources(self):
        self.session.document["tracks"]["video"]["availablePrefixBytes"] = 50
        self.session.publish()
        provider, thread = self.start_provider()
        self.session.document["tracks"]["video"]["availablePrefixBytes"] = 49
        self.session.publish()
        self.wait(lambda: not thread.is_alive())
        self.assertIn("prefix", provider.error)
        self.assertTrue((self.root / "video.mp4.part").exists())

    def test_schema_symlinks_and_aliases_rejected(self):
        document = self.session.document
        for change in (lambda v: v.update(revision=True),
                       lambda v: v["tracks"]["audio"].update(path="../audio"),
                       lambda v: v["tracks"]["audio"].update(state="complete"),
                       lambda v: v["tracks"]["video"].update(availablePrefixBytes=101)):
            value = copy.deepcopy(document)
            change(value)
            with self.assertRaises(ValueError):
                validate_snapshot(value, self.root)
        audio = self.root / "audio.m4a.part"
        audio.unlink()
        audio.symlink_to(self.root / "video.mp4.part")
        with self.assertRaises(ValueError):
            validate_snapshot(document, self.root)
        audio.unlink()
        os.link(self.root / "video.mp4.part", audio)
        with self.assertRaises(ValueError):
            validate_snapshot(document, self.root)

    def test_replaced_file_and_duplicate_provider_are_rejected(self):
        provider, thread = self.start_provider()
        self.wait(lambda: len(provider.file_identities) == 2)
        duplicate = Provider(self.session.path)
        self.assertEqual(duplicate.run(), 1)
        self.assertTrue(read_json(self.root / "provider.json")["ready"])
        replacement = self.root / "new-video"
        replacement.write_bytes(b"x" * 100)
        replacement.replace(self.root / "video.mp4.part")
        self.wait(lambda: not thread.is_alive())
        self.assertIn("replaced", provider.error)
        self.assertTrue((self.root / "video.mp4.part").exists())


if __name__ == "__main__":
    unittest.main()
