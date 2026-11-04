"""Run after build.cmd Release x64. Exercises production objects on localhost only.

python tests/test_ftp_compatibility.py
"""
from pathlib import Path
from datetime import datetime
import os
import posixpath
import socket
import socketserver
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
RELATIVE = "20261103/serial/IMAGE/file.jpg"
TARGET = "/restricted/store/" + RELATIVE
CONTENT = b"synthetic JPG fixture for local FTP compatibility testing"


class FTP(socketserver.StreamRequestHandler):
    def handle(self):
        current = self.server.login_dir
        passive = None
        rename_from = None
        self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        def reply(text):
            self.wfile.write((text + "\r\n").encode("utf-8"))

        def path(argument):
            return posixpath.normpath(posixpath.join(current, argument))

        try:
            reply("220 Local compatibility server")
            while line := self.rfile.readline():
                command, _, argument = line.decode("utf-8").rstrip("\r\n").partition(" ")
                self.server.commands.append((command, argument))
                remote = path(argument)
                if command == "USER":
                    reply("331 Password required")
                elif command == "PASS":
                    reply("530 Login denied " + argument if self.server.deny_login else "230 Logged in")
                elif command == "FEAT":
                    reply("500 Unsupported")
                elif command == "PWD":
                    reply('257 "' + current.replace('"', '""') + '" is current directory')
                elif command in ("TYPE", "NOOP", "OPTS"):
                    reply("200 OK")
                elif command == "CWD":
                    if self.server.relative_only and argument.startswith("/"):
                        reply("553 Absolute directory paths are not accepted")
                        continue
                    if remote == "/" or remote == "/restricted" or remote == self.server.denied:
                        reply("550 Business account cannot enter " + remote)
                    elif remote in self.server.directories:
                        current = remote
                        reply("250 Directory changed")
                    else:
                        reply("550 Directory missing " + remote)
                elif command == "MKD":
                    if self.server.relative_only and argument.startswith("/"):
                        reply("553 Absolute directory paths are not accepted")
                        continue
                    if remote == self.server.denied or posixpath.dirname(remote) not in self.server.directories:
                        reply("550 Cannot create " + remote)
                    else:
                        self.server.directories.add(remote)
                        reply("550 Created concurrently" if self.server.race else '257 "' + remote + '"')
                elif command == "SIZE":
                    if self.server.no_size:
                        reply("502 SIZE unsupported")
                    elif remote in self.server.files:
                        reply("213 " + str(len(self.server.files[remote])))
                    else:
                        reply("550 Missing file")
                elif command == "PASV":
                    passive = socket.socket()
                    passive.bind(("127.0.0.1", 0))
                    passive.listen(1)
                    port = passive.getsockname()[1]
                    reply(f"227 Passive (127,0,0,1,{port // 256},{port % 256})")
                elif command == "STOR":
                    connection, _ = passive.accept()
                    passive.close()
                    passive = None
                    denied = self.server.reject_upload or (self.server.require_path and "/" not in argument)
                    if denied or posixpath.dirname(remote) not in self.server.directories:
                        connection.close()
                        reply("550 STOR rejected by fixture")
                    else:
                        reply("150 Receiving")
                        data = bytearray()
                        with connection:
                            while chunk := connection.recv(65536):
                                data.extend(chunk)
                        self.server.files[remote] = bytes(data)
                        reply("226 Complete")
                elif command == "RNFR":
                    rename_from = remote
                    reply("350 Rename destination" if remote in self.server.files else "550 Missing source")
                elif command == "RNTO":
                    if remote == self.server.denied:
                        reply("550 Rename rejected by fixture")
                    elif remote in self.server.files:
                        reply("550 Existing target")
                    else:
                        self.server.files[remote] = self.server.files.pop(rename_from)
                        reply("250 Renamed")
                elif command == "DELE":
                    if remote in self.server.files:
                        del self.server.files[remote]
                        reply("250 Deleted")
                    else:
                        reply("550 Missing file")
                else:
                    reply("200 OK")
        except (ConnectionError, OSError):
            pass
        finally:
            if passive is not None:
                passive.close()


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


class CompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="ftp-compat-build-")
        cls.probe = Path(cls.build.name) / "probe.exe"
        source = ROOT / "FtpUploaderTool"
        objects = source / "x64" / "Release"
        inputs = [ROOT / "tests" / "FtpCompatibilityProbe.cpp"]
        inputs += [objects / (name + ".obj") for name in
                   ("Common", "Config", "Files", "FtpClient", "RemoteTemp", "Logger", "Worker", "ConnectionTest")]
        command = ('@echo off\r\ncall "%ProgramFiles(x86)%\\Microsoft Visual Studio 10.0\\VC\\vcvarsall.bat" x86_amd64\r\n'
                   'if errorlevel 1 exit /b 1\r\n'
                   'cl /nologo /EHsc /MT /O2 /DUNICODE /D_UNICODE /DNDEBUG /D_CRT_SECURE_NO_WARNINGS '
                   '/I"%taskSource%" ' + " ".join(f'"%taskInput{i}%"' for i in range(len(inputs))) +
                   ' /Feprobe.exe /link /LTCG /SUBSYSTEM:CONSOLE ws2_32.lib crypt32.lib shlwapi.lib shell32.lib ole32.lib comctl32.lib\r\n'
                   'exit /b %errorlevel%\r\n')
        script = Path(cls.build.name) / "build.cmd"
        script.write_bytes(command.encode("ascii"))
        environment = dict(os.environ, taskSource=str(source))
        environment.update({f"taskInput{i}": str(p) for i, p in enumerate(inputs)})
        result = subprocess.run(["cmd", "/c", str(script)], cwd=cls.build.name, env=environment, capture_output=True)
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode("mbcs", errors="replace"))

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.local = tempfile.TemporaryDirectory(prefix="ftp-compat-data-")
        self.root = Path(self.local.name)
        source = self.root / "source" / RELATIVE
        source.parent.mkdir(parents=True)
        source.write_bytes(CONTENT)
        (self.root / "Data").mkdir()
        self.server = Server(("127.0.0.1", 0), FTP)
        self.server.directories = {"/restricted/store"}
        self.server.files = {}
        self.server.commands = []
        self.server.require_path = True
        self.server.race = self.server.no_size = self.server.reject_upload = self.server.deny_login = False
        self.server.denied = None
        self.server.relative_only = False
        self.server.login_dir = "/"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.local.cleanup()

    def run_probe(self, *, mode="worker", base="/restricted/store", temp=False, policy=0, expected=True,
                  password="fixture-secret"):
        args = [self.probe, mode, str(self.server.server_address[1]), self.root, base,
                str(int(temp)), str(policy), str(int(expected)), password]
        result = subprocess.run(args, capture_output=True, timeout=16)
        text = result.stdout.decode("utf-8", errors="replace")
        self.assertEqual(result.returncode, 0, text + result.stderr.decode("utf-8", errors="replace"))
        self.assertEqual((self.root / "source" / RELATIVE).read_bytes(), CONTENT)
        return text

    def test_missing_tree_without_root_or_ancestor_access(self):
        self.run_probe()
        self.assertEqual(self.server.files[TARGET], CONTENT)
        self.assertIn(("STOR", RELATIVE), self.server.commands)
        self.assertFalse(any(command == "CWD" and arg in ("/", "/restricted") for command, arg in self.server.commands))

    def test_existing_tree_and_relative_stor(self):
        self.server.directories.update({posixpath.dirname(TARGET)})
        self.run_probe()
        self.assertFalse(any(command == "MKD" for command, _ in self.server.commands))
        self.assertIn(("SIZE", RELATIVE), self.server.commands)

    def test_temp_rename_and_overwrite(self):
        self.server.files[TARGET] = b"old"
        self.run_probe(temp=True)
        self.assertEqual(self.server.files[TARGET], CONTENT)
        self.assertIn(("DELE", RELATIVE), self.server.commands)
        self.assertIn(("RNTO", RELATIVE), self.server.commands)
        self.assertFalse(list((self.root / "Data" / "RemoteTemp").glob("*.ini")))

    def test_skip_existing_preserves_source(self):
        self.server.files[TARGET] = b"old"
        self.run_probe(policy=2, expected=False)
        self.assertEqual(self.server.files[TARGET], b"old")
        self.assertFalse(any(command == "STOR" for command, _ in self.server.commands))

    def test_rename_existing_uses_relative_destination(self):
        self.server.files[TARGET] = b"old"
        self.run_probe(policy=1)
        self.assertEqual(self.server.files[TARGET], b"old")
        self.assertIn(CONTENT, self.server.files.values())

    def test_directory_denied_has_command_and_server_reply(self):
        self.server.denied = "/restricted/store/20261103"
        text = self.run_probe(expected=False)
        self.assertIn("CWD /restricted/store/20261103", text)
        self.assertIn("550 Business account cannot enter", text)
        self.assertFalse(any(command == "STOR" for command, _ in self.server.commands))

    def test_upload_denied_keeps_diagnostic_after_temp_cleanup(self):
        self.server.reject_upload = True
        text = self.run_probe(temp=True, expected=False)
        self.assertIn("STOR 20261103/serial/IMAGE/_ftpupload_", text)
        self.assertIn("550 STOR rejected by fixture", text)

    def test_rename_denied_keeps_diagnostic_after_cleanup(self):
        self.server.denied = TARGET
        text = self.run_probe(temp=True, expected=False)
        self.assertIn("RNTO " + RELATIVE, text)
        self.assertIn("550 Rename rejected by fixture", text)
        self.assertFalse(self.server.files)

    def test_placeholder_base_and_nested_relative_path(self):
        self.run_probe(base="/restricted/store/{yyyy}/{MM}")
        paths = [argument for command, argument in self.server.commands if command == "STOR"]
        self.assertRegex(paths[0], r"^\d{4}/\d{2}/" + RELATIVE + "$")
        self.assertIn(CONTENT, self.server.files.values())

    def test_concurrent_directory_creation(self):
        self.server.race = True
        self.run_probe()
        self.assertEqual(self.server.files[TARGET], CONTENT)

    def test_connection_test_relative_upload_size_delete(self):
        self.run_probe(mode="connection", base="/restricted/store/{yyyy}")
        self.assertFalse(self.server.files)
        commands = self.server.commands
        stored = next(argument for command, argument in commands if command == "STOR")
        self.assertRegex(stored, r"^\d{4}/_ftptest_.*\.tmp$")
        self.assertIn(("SIZE", stored), commands)
        self.assertIn(("DELE", stored), commands)

    def test_login_credentials_never_in_error(self):
        self.server.deny_login = True
        text = self.run_probe(mode="login", expected=False)
        self.assertIn("PASS", text)
        self.assertNotIn("fixture-secret", text)
        self.assertNotIn("530 Login denied", text)

    def test_unsupported_size_blocks_non_overwrite(self):
        self.server.no_size = True
        self.run_probe(policy=2, expected=False)
        self.assertFalse(any(command == "STOR" for command, _ in self.server.commands))

    def test_root_access_denial_is_not_success(self):
        text = self.run_probe(mode="directory", base="/", expected=False)
        self.assertIn("CWD /", text)
        self.assertIn("550 Business account cannot enter /", text)

    def relative_server(self):
        self.server.login_dir = "/home/business"
        self.server.directories = {self.server.login_dir, self.server.login_dir + "/IT_Display/ASY/TA200/123"}
        return "IT_Display/ASY/TA200/123/"

    def test_relative_base_uses_login_directory(self):
        base = self.relative_server()
        self.run_probe(base=base)
        target = self.server.login_dir + "/" + base + RELATIVE
        self.assertEqual(self.server.files[target], CONTENT)
        self.assertIn(("CWD", base.rstrip("/")), self.server.commands)
        self.assertFalse(any(cmd == "CWD" and arg.startswith("/IT_Display") for cmd, arg in self.server.commands))

    def test_relative_temp_overwrite_and_cleanup(self):
        base = self.relative_server()
        target = self.server.login_dir + "/" + base + RELATIVE
        self.server.files[target] = b"old"
        self.run_probe(base=base, temp=True)
        self.assertEqual(self.server.files, {target: CONTENT})
        self.assertIn(("DELE", RELATIVE), self.server.commands)

    def test_relative_temp_failure_cleans_exact_login_based_path(self):
        base = self.relative_server()
        self.server.denied = self.server.login_dir + "/" + base + RELATIVE
        self.run_probe(base=base, temp=True, expected=False)
        self.assertFalse(self.server.files)
        deleted = [arg for cmd, arg in self.server.commands if cmd == "DELE"]
        self.assertTrue(any(arg.startswith(self.server.login_dir + "/" + base) for arg in deleted))
        self.assertFalse(list((self.root / "Data" / "RemoteTemp").glob("*.ini")))

    def test_relative_connection_test(self):
        base = self.relative_server()
        self.run_probe(mode="connection", base=base + "{yyyy}")
        self.assertFalse(self.server.files)
        self.assertTrue(any(cmd == "STOR" for cmd, _ in self.server.commands))

    def test_relative_repeated_prepare_does_not_nest_base(self):
        base = self.relative_server()
        self.run_probe(mode="directory", base=base.rstrip("/"))
        self.assertFalse(any(cmd == "MKD" for cmd, _ in self.server.commands))

    def test_relative_template_without_fixed_prefix(self):
        self.server.login_dir = "/home/business"
        self.server.directories = {self.server.login_dir}
        self.run_probe(base="{yyyy}/{MM}")
        self.assertEqual(len(self.server.files), 1)
        self.assertTrue(next(iter(self.server.files)).startswith("/home/business/"))

    def assert_relative_directory_commands(self):
        self.assertFalse(any(cmd in ("CWD", "MKD") and arg.startswith("/")
                             for cmd, arg in self.server.commands))

    def test_relative_only_server_creates_missing_base_and_nested_tree(self):
        base = self.relative_server()
        self.server.directories = {self.server.login_dir}
        self.server.relative_only = True
        self.run_probe(base=base, temp=True)
        self.assertEqual(self.server.files, {self.server.login_dir + "/" + base + RELATIVE: CONTENT})
        self.assert_relative_directory_commands()

    def test_relative_only_server_uploads_multiple_existing_directories(self):
        base = self.relative_server()
        self.server.relative_only = True
        second = "20261104/serial/DATA/second.csv"
        source = self.root / "source" / second
        source.parent.mkdir(parents=True)
        source.write_bytes(CONTENT)
        prefix = self.server.login_dir + "/" + base
        self.server.directories.update({posixpath.dirname(prefix + RELATIVE),
                                        posixpath.dirname(prefix + second)})
        self.run_probe(mode="worker-multiple", base=base, temp=True)
        self.assertEqual(self.server.files, {prefix + RELATIVE: CONTENT, prefix + second: CONTENT})
        self.assert_relative_directory_commands()

    def test_relative_only_connection_test_with_template(self):
        base = self.relative_server()
        self.server.relative_only = True
        self.run_probe(mode="connection", base=base + "{yyyy}")
        self.assertFalse(self.server.files)
        self.assert_relative_directory_commands()

    def test_relative_only_template_without_fixed_prefix(self):
        self.server.login_dir = "/home/business"
        self.server.directories = {self.server.login_dir}
        self.server.relative_only = True
        self.run_probe(base="{yyyy}/{MM}")
        self.assertIn(CONTENT, self.server.files.values())
        self.assert_relative_directory_commands()

    def test_absolute_template_without_prefix_does_not_require_root_access(self):
        year = "/" + str(datetime.now().year)
        self.server.directories = {year, year + "/" + posixpath.dirname(RELATIVE)}
        self.server.require_path = False
        self.run_probe(base="/{yyyy}")
        self.assertEqual(self.server.files, {year + "/" + RELATIVE: CONTENT})
        self.assertFalse(any(cmd == "CWD" and arg == "/" for cmd, arg in self.server.commands))

    def test_absolute_template_connection_does_not_require_root_access(self):
        self.server.directories = {"/" + str(datetime.now().year)}
        self.server.require_path = False
        self.run_probe(mode="connection", base="/{yyyy}")
        self.assertFalse(self.server.files)
        self.assertFalse(any(cmd == "CWD" and arg == "/" for cmd, arg in self.server.commands))


if __name__ == "__main__":
    unittest.main(verbosity=2)
