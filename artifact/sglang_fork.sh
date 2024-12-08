# fork and generate the code. cut unnecessary parts, size startup overhead vs. inference latency compared with vanilla gpt 

#!/usr/bin/env python3
import argparse
import subprocess
import time
import signal
import sys

def run_command(cmd, input_str=None):
    """Run a command and optionally provide input. Returns (stdout, stderr)."""
    proc = subprocess.Popen(
        cmd,
        shell=True,
        stdin=subprocess.PIPE if input_str else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    out, err = proc.communicate(input_str)
    return out, err, proc.returncode

def main():
    parser = argparse.ArgumentParser(description="Measure MVVM startup overhead and inference latency.")
    parser.add_argument("--mvvm_checkpoint_cmd", default="./MVVM_checkpoint", help="Command for MVVM_checkpoint")
    parser.add_argument("--mvvm_restore_cmd", default="./MVVM_restore", help="Command for MVVM_restore")
    parser.add_argument("--wasm_target", default="llama.wasm", help="WASM target file")
    parser.add_argument("--llm_model", default="llama32_1b.bin", help="LLM model file")
    parser.add_argument("--vanilla_gpt_cmd", default="./vanilla_gpt --model llama32_1b.bin", help="Vanilla GPT command for baseline comparison")
    parser.add_argument("--sleep_time", type=float, default=2.0, help="Seconds to wait for MVVM interactive mode initialization")
    parser.add_argument("--test_prompt", default="Some test prompt.", help="Prompt to feed for inference")
    args = parser.parse_args()

    # Start MVVM_checkpoint in interactive mode and measure startup time
    print("Starting MVVM_checkpoint in interactive mode...")
    start_time = time.time_ns() // 1_000_000  # current time in ms
    proc = subprocess.Popen(
        f"{args.mvvm_checkpoint_cmd} -t {args.wasm_target} -a \"{args.llm_model},-i\"",
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    # Give it some time to initialize
    time.sleep(args.sleep_time)

    # Simulate Ctrl-C to snapshot
    print("Taking snapshot (simulating Ctrl-C)...")
    proc.send_signal(signal.SIGINT)
    proc.wait()

    end_time = time.time_ns() // 1_000_000
    checkpoint_startup_ms = end_time - start_time
    print(f"MVVM checkpoint startup overhead: {checkpoint_startup_ms}ms")

    # Restore session and measure inference latency
    print("Restoring from snapshot...")
    start_time = time.time_ns() // 1_000_000
    out, err, rc = run_command(f"{args.mvvm_restore_cmd} -t {args.wasm_target}", input_str=args.test_prompt)
    end_time = time.time_ns() // 1_000_000
    restore_latency_ms = end_time - start_time
    print(f"MVVM inference latency after restore: {restore_latency_ms}ms")

    # Compare with vanilla GPT baseline
    print("Measuring vanilla GPT performance...")
    start_time = time.time_ns() // 1_000_000
    out, err, rc = run_command(args.vanilla_gpt_cmd, input_str=args.test_prompt)
    end_time = time.time_ns() // 1_000_000
    vanilla_latency_ms = end_time - start_time
    print(f"Vanilla GPT inference latency: {vanilla_latency_ms}ms")

    # Print summary
    print("======== SUMMARY ========")
    print(f"MVVM startup overhead:       {checkpoint_startup_ms}ms")
    print(f"MVVM inference after restore:{restore_latency_ms}ms")
    print(f"Vanilla GPT inference:       {vanilla_latency_ms}ms")
    print("==========================")

if __name__ == "__main__":
    main()
