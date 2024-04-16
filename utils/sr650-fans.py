#!/usr/bin/env python

# Copied from
# https://stackoverflow.com/questions/35821184/implement-an-interactive-shell-over-ssh-in-python-using-paramiko

import paramiko
import time


class ShellHandler:

    def __init__(self, host, user, psw):
        self.ssh = paramiko.SSHClient()
        self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.ssh.connect(host, username=user, password=psw, port=22)

        channel = self.ssh.invoke_shell()
        self.stdin = channel.makefile("wb")
        self.stdout = channel.makefile("r")

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
            time.sleep(0.5)
        self.stdin.flush()

        lines = []
        for line in self.stdout:
            line = line.strip("\r\n")
            print(f"rx <<<{line}>>>")
            lines.append(line)
            if line == "ok" or line.startswith("Error:"):
                break

        return lines


if __name__ == "__main__":
    print("start")
    sh = ShellHandler("10.0.0.139", "USERID", "SR650_7x06")

    crit_interval = 120
    crit_temp = {
        "Exhaust Temp": 40.0,
        "CPU1 Temp": 60.0,
        "CPU2 Temp": 60.0,
        "PCH Temp": 60.0,
    }

    sleep_time = 30
    while True:
        response = sh.execute(["temps", "fans", "thermal -table 10de2206 1"])
        for line in response:
            # print(f"with {line}")
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
                sleep_time = crit_interval

        print(f"sleeping for {sleep_time}s")
        time.sleep(sleep_time)
