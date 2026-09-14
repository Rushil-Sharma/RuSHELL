import subprocess
import time
import sys
import os
import pty
import select

def run_test(command_to_send, scheduler=None, timeout=25):
    # Ensure build is up to date
    build_cmd = ["make"]
    if scheduler:
        build_cmd.append(f"SCHEDULER={scheduler}")
    subprocess.run(build_cmd, check=True)

    master_fd, slave_fd = pty.openpty()

    cmd = ["make", "qemu"]
    if scheduler:
        cmd.append(f"SCHEDULER={scheduler}")

    proc = subprocess.Popen(
        cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True
    )
    os.close(slave_fd)

    output = ""
    command_sent = False
    start_time = time.time()

    while time.time() - start_time < timeout:
        r, _, _ = select.select([master_fd], [], [], 0.5)
        if r:
            try:
                data = os.read(master_fd, 1024).decode('utf-8', errors='ignore')
                output += data
                print(data, end="", flush=True)
            except OSError:
                break

            if not command_sent and "$ " in output:
                time.sleep(0.5)
                os.write(master_fd, (command_to_send + "\n").encode('utf-8'))
                command_sent = True

            if command_sent and ("passed" in output or "SUMMARY METRICS" in output or "METRICS" in output):
                # Wait a bit to capture trailing output
                time.sleep(1)
                while True:
                    r2, _, _ = select.select([master_fd], [], [], 0.5)
                    if r2:
                        try:
                            d2 = os.read(master_fd, 1024).decode('utf-8', errors='ignore')
                            output += d2
                            print(d2, end="", flush=True)
                        except OSError:
                            break
                    else:
                        break
                break

    try:
        os.write(master_fd, b"\x01x")
    except Exception:
        pass

    try:
        proc.terminate()
        proc.wait(timeout=2)
    except Exception:
        proc.kill()

    os.close(master_fd)
    return output

if __name__ == "__main__":
    test_name = sys.argv[1] if len(sys.argv) > 1 else "testmlfq"
    sched = sys.argv[2] if len(sys.argv) > 2 else "MLFQ"
    run_test(test_name, sched)
