#!/usr/bin/env python

# Copied from
# https://stackoverflow.com/questions/35821184/implement-an-interactive-shell-over-ssh-in-python-using-paramiko

import paramiko
import time
from threading import Thread


class ShellHandler:

    def __init__(self, host, user, psw):
        self.ssh = paramiko.SSHClient()
        self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.ssh.connect(host, username=user, password=psw, port=22)

        channel = self.ssh.invoke_shell()
        self.stdin = channel.makefile("wb")
        self.stdout = channel.makefile("r")
        self.finished = False
        self.sleep_time = 30

    def __del__(self):
        self.ssh.close()

    def execute(self, cmds):
        """
        :param cmd: the command to be executed on the remote computer
        :examples:  execute('ls')
                    execute('finger')
                    execute('cd folder_name')
        """
        for cmd in cmds:
            print(f"tx <<<{cmd}>>>")
            self.stdin.write(cmd + "\n")
            self.stdin.flush()
            time.sleep(0.5)

        lines = []
        for line in self.stdout:
            line = line.strip("\r\n")
            print(f"rx <<<{line}>>>")
            lines.append(line)
            if line == "ok" or line.startswith("Error:"):
                break
        return lines

    def finish(self):
        finished = True

    def typersleep(self, seconds):
        sleep_time = seconds

    def typer(self, cmds):
        while not self.finished:
            for cmd in cmds:
                print(f"tx <<<{cmd}>>>")
                self.stdin.write(cmd + "\n")
                self.stdin.flush()
                time.sleep(0.5)
            print(f"sleeping for {self.sleep_time}s")
            time.sleep(self.sleep_time)

    def monitor(self):
        lines = []
        for line in self.stdout:
            line = line.strip("\r\n")
            print(f"rx <<<{line}>>>")
            lines.append(line)
            if line == "ok" or line.startswith("Error:"):
                break

        return lines


def sr_handler():
    print("start sr_handler")
    sh = ShellHandler("10.0.0.139", "USERID", "mouse")

    crit_interval = 120
    crit_temp = {
        "Exhaust Temp": 40.0,
        "CPU1 Temp": 60.0,
        "CPU2 Temp": 60.0,
        "PCH Temp": 60.0,
    }

    typer_thread = Thread(
        target=sh.typer, args=(["temps", "fans", "thermal -table 10de2206 1"],)
    )
    typer_thread.start()

    running = True
    while running:
        response = sh.monitor()
        for line in response:
            # print(f"with {line}")
            if line.startswith("Error:"):
                running = False
            if len(line) < 60:
                continue
            name = line[0:17].strip()
            if not name:
                continue
            temps = [t.split("/")[1] for t in line[17:].split() if "/" in t]
            if len(temps) < 3:
                continue
            temp = float(temps[2])

            print(f"name: '{name}' temp/C: {temp} {'*' if name in crit_temp else ''}")

            if name in crit_temp and temp >= crit_temp[name]:
                sh.typersleep(120)
            else:
                sh.typersleep(30)

    sh.finish()
    typer_thread.join()


if __name__ == "__main__":
    while True:
        sr_handler()
