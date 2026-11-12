import subprocess
import os
import sys
import time

bin_dir = os.path.abspath(r'pc/build64/bin')
exe = os.path.join(bin_dir, 'AnimalCrossing.exe')
stdout_log_path = os.path.join(bin_dir, 'servers', 'default', 'logs', 'stdout.log')
cmd_path = os.path.join(bin_dir, 'servers', 'default', 'cmd.txt')

os.makedirs(os.path.dirname(stdout_log_path), exist_ok=True)

with open(stdout_log_path, 'w', encoding='utf-8', errors='replace') as log_file:
    p = subprocess.Popen(
        [exe, '--host', '7777', '--dedicated', '--verbose'],
        cwd=bin_dir,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding='utf-8',
        errors='replace',
        bufsize=1
    )

    print(f"Dedicated server started with PID {p.pid}")
    sys.stdout.flush()

    # Read output and check for cmd.txt in a non-blocking way
    import threading

    def reader():
        for line in p.stdout:
            log_file.write(line)
            log_file.flush()
            if 'Server ready.' in line or 'observer active' in line or '[SERVER]' in line or '[GUEST]' in line or 'peer' in line.lower():
                print(line.rstrip())
                sys.stdout.flush()

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    try:
        while p.poll() is None:
            if os.path.exists(cmd_path):
                try:
                    with open(cmd_path, 'r', encoding='utf-8-sig', errors='replace') as cf:
                        cmds = cf.read()
                    os.remove(cmd_path)
                    if cmds:
                        for c in cmds.splitlines():
                            c = c.strip()
                            if c:
                                print(f"Executing server command: {c}")
                                p.stdin.write(c + '\n')
                                p.stdin.flush()
                except Exception as ex:
                    print(f"Error reading cmd.txt: {ex}")
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("Stopping server via SIGINT...")
        p.stdin.write('stop\n')
        p.stdin.flush()
        p.wait(timeout=10)

print(f"Dedicated server process exited with code {p.returncode}")
