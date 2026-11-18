#!/usr/bin/env python3
"""(build 1.00, quickstart) A stand-in server for MGO1 Host's QUICK SETUP test (tests/panel_quick_go.cpp, run by
tests/test_quick_setup.py): an SSH server on 127.0.0.1 (paramiko: password sign-in, exec channels, scp -t) whose commands
run in a sandbox folder, never on this machine's own /opt, /etc or /root:
  - the program's paths are mapped: /root/mgo1host-install -> <root>/stage, /opt/mgo1host -> <root>/opt;
  - PATH starts with <root>/bin: before the installation it has no mgo1host-agent (the program sees no agent), after it the
    stand-in install.sh puts there a wrapper that runs the real host/mgo1host-agent on the sandbox's folders
    (MGO1_CONF_DIR, MGO1_STATE_DIR, ...), and a stand-in systemctl (tests/quick/systemctl);
  - scp -t receives the program's uploads (its own small implementation of the sink side of SCP).
Each exec is logged in <root>/commands.log (the password never is).
    python3 tests/fake_ssh_host.py --root <sandbox> --port 2222 --password <pw> [--key <host key file>]
Needs paramiko (pip install paramiko); tests/test_quick_setup.py says so and stops when it is missing.
"""
import argparse, os, socket, subprocess, sys, threading, time

import paramiko


def mapped(cmd, root):
    return cmd.replace("/root/mgo1host-install", os.path.join(root, "stage")).replace("/opt/mgo1host", os.path.join(root, "opt"))


class Server(paramiko.ServerInterface):
    def __init__(self, password):
        self.password = password
        self.cv = threading.Condition()
        self.commands = {}                                        # channel id -> its command

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def get_allowed_auths(self, username):
        return "password"

    def check_auth_password(self, username, password):
        return paramiko.AUTH_SUCCESSFUL if username == "root" and password == self.password else paramiko.AUTH_FAILED

    def check_channel_exec_request(self, channel, command):
        with self.cv:
            self.commands[channel.get_id()] = command.decode("utf-8", "replace")
            self.cv.notify_all()
        return True

    def command_of(self, chan, timeout):
        with self.cv:
            self.cv.wait_for(lambda: chan.get_id() in self.commands, timeout)
            return self.commands.pop(chan.get_id(), None)


def scp_sink(chan, path):
    """scp -t <path>: one file (libssh2_scp_send64: "C<mode> <size> <name>\\n", its bytes, a zero byte)."""
    def readline():
        b = b""
        while not b.endswith(b"\n"):
            c = chan.recv(1)
            if not c:
                return None
            b += c
        return b
    chan.sendall(b"\0")
    line = readline()
    while line is not None and line.startswith(b"T"):            # (times: not sent by MGO1 Host)
        chan.sendall(b"\0")
        line = readline()
    if line is None or not line.startswith(b"C"):
        return 1
    mode, size, _name = line[1:].decode().strip().split(" ", 2)
    size = int(size)
    chan.sendall(b"\0")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    left = size
    with open(path, "wb") as f:
        while left > 0:
            data = chan.recv(min(1 << 16, left))
            if not data:
                return 1
            f.write(data)
            left -= len(data)
    os.chmod(path, int(mode, 8))
    chan.recv(1)                                                  # its zero byte
    chan.sendall(b"\0")
    return 0


def run(chan, command, root, env, log):
    cmd = mapped(command, root)
    with open(log, "a") as f:
        f.write("%s %s\n" % (time.strftime("%H:%M:%S"), cmd[:300]))
    if cmd.startswith("scp -t "):
        path = cmd[len("scp -t "):].strip().strip("'")
        rc = scp_sink(chan, path)
        chan.send_exit_status(rc)
        return
    p = subprocess.Popen(["/bin/sh", "-c", cmd], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, cwd=root)

    def pump_in():
        try:
            while True:
                data = chan.recv(65536)
                if not data:
                    break
                p.stdin.write(data)
                p.stdin.flush()
        except (OSError, ValueError):
            pass
        try:
            p.stdin.close()
        except OSError:
            pass

    def pump_err():
        for data in iter(lambda: p.stderr.read1(65536), b""):
            try:
                chan.sendall_stderr(data)
            except OSError:
                break

    threading.Thread(target=pump_in, daemon=True).start()
    te = threading.Thread(target=pump_err, daemon=True)
    te.start()
    try:
        for data in iter(lambda: p.stdout.read1(65536), b""):
            chan.sendall(data)
    except OSError:                                               # the program closed the channel: the command is stopped
        p.kill()
    rc = p.wait()
    te.join(2)
    try:
        chan.send_exit_status(rc)
    except OSError:
        pass


def serve(client, key, password, root, env, log):
    t = paramiko.Transport(client)
    t.add_server_key(key)
    srv = Server(password)
    try:
        t.start_server(server=srv)
    except paramiko.SSHException:
        return
    while t.is_active():
        chan = t.accept(timeout=1)
        if chan is None:
            continue
        threading.Thread(target=channel, args=(chan, srv, root, env, log), daemon=True).start()


def channel(chan, srv, root, env, log):
    command = srv.command_of(chan, 10)
    if command is None:
        chan.close()
        return
    try:
        run(chan, command, root, env, log)
    finally:
        try:
            chan.shutdown_write()
            chan.close()
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=2222)
    ap.add_argument("--password", required=True)
    ap.add_argument("--key", default="")
    a = ap.parse_args()
    root = os.path.realpath(a.root)
    for d in ("stage", "opt", "bin", "etc", "var", "log", "systemd", "svc"):
        os.makedirs(os.path.join(root, d), exist_ok=True)
    keyfile = a.key or os.path.join(root, "hostkey")
    if not os.path.exists(keyfile):
        paramiko.RSAKey.generate(2048).write_private_key_file(keyfile)
    key = paramiko.RSAKey(filename=keyfile)
    env = dict(os.environ, PATH=os.path.join(root, "bin") + ":/usr/local/bin:/usr/bin:/bin", FAKE_ROOT=root)
    log = os.path.join(root, "commands.log")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", a.port))
    sock.listen(16)
    print("fake ssh host: 127.0.0.1:%d, root %s" % (a.port, root), flush=True)
    while True:
        client, _ = sock.accept()
        threading.Thread(target=serve, args=(client, key, a.password, root, env, log), daemon=True).start()


if __name__ == "__main__":
    main()
