import subprocess
import multiprocessing
import time
import os
import signal

# --- CONFIGURATION ---
SCRIPT_NAME = "bitforce.py"
NUM_WORKERS = 12


# ---------------------

def run_racer(worker_id, stop_event):
    """
    Continually retries the script.
    Stops immediately if stop_event is set by another worker.
    """
    attempt = 1

    # We use Popen so we have a handle to kill the subprocess if the race ends mid-run
    while not stop_event.is_set():
        print(f"[Worker {worker_id}] Starting attempt {attempt}...")

        # Start the expensive script
        with subprocess.Popen(
                ["lien/bin/python", SCRIPT_NAME],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                preexec_fn=os.setsid  # logic to help kill process trees later (Linux/Mac)
        ) as process:

            try:
                # Wait for script to finish OR for the stop_event to be set
                while process.poll() is None:
                    if stop_event.is_set():
                        # Another worker won! Kill this attempt and exit.
                        process.terminate()
                        return
                    time.sleep(0.5)  # Check every 0.5 seconds

                # Script finished. Check exit code.
                stdout, stderr = process.communicate()

                if process.returncode == 0:
                    # WE WON THE RACE!
                    print(f"🏆 [Worker {worker_id}] SUCCESS! Saving output and stopping others.")

                    # 1. Save the output
                    with open(f"success_output_worker_{worker_id}.txt", "w") as f:
                        f.write(stdout)

                    # 2. Signal everyone else to stop
                    stop_event.set()
                    return
                else:
                    # Failed (Exit code 1). Retry loop continues.
                    print(f"⚠️ [Worker {worker_id}] Failed (Exit {process.returncode}). Retrying...")
                    attempt += 1

            except Exception as e:
                # Safety net to ensure subprocess dies if Python crashes
                process.kill()
                raise e


if __name__ == "__main__":
    # Create an event that acts as the "Finish Line" flag
    stop_event = multiprocessing.Event()

    workers = []
    print(f"🏁 Starting race with {NUM_WORKERS} workers...")

    for i in range(NUM_WORKERS):
        # daemon=True ensures workers die if the main script is killed
        p = multiprocessing.Process(target=run_racer, args=(i, stop_event))
        p.daemon = True
        workers.append(p)
        p.start()

    # The main thread just waits for the flag to be raised
    stop_event.wait()

    print("\n🛑 Race finished. Terminating lingering workers...")

    # Optional: Give workers a moment to see the flag and exit cleanly
    time.sleep(1)

    # Force kill anyone still running (to save CPU immediately)
    for p in workers:
        if p.is_alive():
            p.terminate()

    print("Done.")