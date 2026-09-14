import multiprocessing
import os
from pathlib import Path
import signal
import subprocess
import sys

# --- CONFIGURATION ---
SCRIPT_NAME = "bitforce.py"
NUM_WORKERS = 12
# ---------------------

SCRIPT_DIR = Path(__file__).resolve().parent
POLL_INTERVAL = 0.2


def configure_console():
    """Keep diagnostics printable with legacy Windows console encodings."""
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="backslashreplace")


def terminate_process_tree(process):
    """Stop an attempt and its children before its worker exits."""
    if process.poll() is not None:
        return

    if os.name == "nt":
        try:
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW,
                timeout=10,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    # Fall back to stopping the direct child if tree termination failed.
    if process.poll() is None:
        process.kill()
    process.wait(timeout=5)


def run_racer(worker_id, stop_event, winner_lock):
    """Retry until this worker succeeds or another worker stops the race."""
    # The parent handles Ctrl+C and gives every worker time to clean up.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    configure_console()
    attempt = 1
    child_env = os.environ.copy()
    child_env["PYTHONIOENCODING"] = "utf-8"
    process_options = (
        {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
        if os.name == "nt"
        else {"start_new_session": True}
    )

    while not stop_event.is_set():
        print(f"[Worker {worker_id}] Starting attempt {attempt}...", flush=True)

        # Use the active Python/virtual environment, with paths relative to run.py.
        # UTF-8 also allows the child script to print Unicode on Windows.
        with subprocess.Popen(
            [sys.executable, "-X", "utf8", str(SCRIPT_DIR / SCRIPT_NAME)],
            cwd=SCRIPT_DIR,
            env=child_env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
            errors="replace",
            **process_options,
        ) as process:
            try:
                while True:
                    if stop_event.is_set():
                        return
                    try:
                        # Drain both pipes while waiting so verbose scripts cannot
                        # block on a full pipe. Retrying communicate retains output.
                        stdout, stderr = process.communicate(timeout=POLL_INTERVAL)
                        break
                    except subprocess.TimeoutExpired:
                        continue

                if process.returncode == 0:
                    with winner_lock:
                        if stop_event.is_set():
                            return
                        output_path = SCRIPT_DIR / f"success_output_worker_{worker_id}.txt"
                        output_path.write_text(stdout, encoding="utf-8")
                        print(
                            f"[Worker {worker_id}] SUCCESS! Saved output to {output_path}",
                            flush=True,
                        )
                        stop_event.set()
                    return

                print(
                    f"[Worker {worker_id}] Failed (Exit {process.returncode}). Retrying...",
                    flush=True,
                )
                # Include a short diagnostic for dependency/runtime errors.
                if stderr.strip():
                    print(stderr[-2000:], file=sys.stderr, flush=True)
                attempt += 1
            finally:
                terminate_process_tree(process)


def main():
    configure_console()
    if NUM_WORKERS < 1:
        print("NUM_WORKERS must be at least 1.", file=sys.stderr)
        return 1
    if not (SCRIPT_DIR / SCRIPT_NAME).is_file():
        print(f"Script not found: {SCRIPT_DIR / SCRIPT_NAME}", file=sys.stderr)
        return 1

    stop_event = multiprocessing.Event()
    winner_lock = multiprocessing.Lock()
    workers = []
    interrupted = False
    succeeded = False
    print(f"Starting race with {NUM_WORKERS} workers...", flush=True)

    try:
        for i in range(NUM_WORKERS):
            worker = multiprocessing.Process(
                target=run_racer, args=(i, stop_event, winner_lock)
            )
            worker.start()
            workers.append(worker)

        while not stop_event.wait(POLL_INTERVAL):
            # A crashed worker must not leave the parent waiting forever.
            if any(worker.exitcode not in (None, 0) for worker in workers):
                break
            if not any(worker.is_alive() for worker in workers):
                break
        succeeded = stop_event.is_set()
    except KeyboardInterrupt:
        interrupted = True
        print("\nInterrupted. Stopping attempts...", flush=True)
    finally:
        # Do not terminate multiprocessing workers: they own subprocess cleanup.
        previous_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            stop_event.set()
            for worker in workers:
                worker.join()
        finally:
            signal.signal(signal.SIGINT, previous_handler)

    print("Done.", flush=True)
    if interrupted:
        return 130
    return 0 if succeeded and all(worker.exitcode == 0 for worker in workers) else 1


if __name__ == "__main__":
    multiprocessing.freeze_support()
    sys.exit(main())
