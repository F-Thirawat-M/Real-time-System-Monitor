# Week 15 Demo Script

## Preparation

1. Use Linux or WSL and run Mini-htop as a normal user.
2. Install the requirements and run the automated tests.
3. Open two terminals: one for Mini-htop and one for a harmless test process.

## Demonstration order

1. Start the application with `python main.py`.
2. Point out total CPU, per-core CPU, RAM, swap, and disk gauges.
3. Confirm that the status bar displays `source: /proc`.
4. Explain that `/proc/stat` and `/proc/meminfo` are read using `os.open()`,
   `os.read()`, and `os.close()`.
5. Demonstrate process sorting with `C`, `M`, `P`, and `N`.
6. Press `/` and search for a process name or PID.
7. In the second terminal, create a harmless test process:

   ```bash
   python -c "import time; time.sleep(300)"
   ```

8. Find the test process and send `SIGTERM`. Explain graceful termination.
9. Create the test process again and demonstrate `SIGKILL`. Explain that it
   cannot be handled or ignored.
10. Finish with the architecture, error handling, limitations, and future work.

## Safety checklist

- Never run the monitor as root for the demonstration.
- Signal only the test process created by the team.
- Verify the PID and process name before confirming.
- Do not signal desktop, shell, IDE, or operating-system processes.
