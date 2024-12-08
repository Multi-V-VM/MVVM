import os
import subprocess
from typing import Dict, Union, Optional
import time
import openai
import matplotlib.pyplot as plt
import numpy as np

def get_llama32_wasm_simd(edge_model_path="./MVVM_checkpoint", edge_model_bin="./llama32_1b.bin", prompt="Hello, world!"):
    cmd = [
        edge_model_path,
        "-t", "./llama.aot",
        "-a", f"{edge_model_bin},-i,\'{prompt}\'"
    ]
    
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=30
    )
    
    if result.returncode != 0:
        raise Exception(f"Edge model error: {result.stderr}")
        
    tokens = 0
    for r in result.stdout.split("\n"):
        if r.startswith("Token"):
            tokens = int(r.replace("s","").replace("Token",""))
    return tokens

def get_llama_wasi_nn(edge_model_path="./iwasm", edge_model_bin="./Llama-3.2-1B-Instruct-Q8_0.gguf", prompt="Hello, world!"):
    cmd = [
        edge_model_path, 
        f"{edge_model_bin}", f"\'{prompt}\'"
    ]
    
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=30
    )
    
    tokens = 0
    for r in result.stdout.split("\n"):
        if r.startswith("Token"):
            tokens = int(r.replace("s","").replace("Token",""))
    return tokens

def get_openai_latency(
    api_key: str,
    model: str = "gpt-3.5-turbo",
    prompt: str = "Hello, world!",
    max_tokens: int = 100,
    temperature: float = 0.7
) -> Dict[str, Union[int, float]]:
    openai.api_key = api_key
    start_time = time.time()
    
    response = openai.ChatCompletion.create(
        model=model,
        messages=[{"role": "user", "content": prompt}],
        max_tokens=max_tokens,
        temperature=temperature
    )
    print(response)
    end_time = time.time()
    return {
        "tokens": response.usage.completion_tokens,
        "latency": end_time - start_time,
        "success": True
    }

def run_comparison_test(
    prompt: str = "Hello, world!",
    openai_model: str = "gpt-3.5-turbo",
    openai_api_key: Optional[str] = None,
    wasm_path: str = "./MVVM_checkpoint",
    wasm_bin: str = "./llama32_1b.bin",
    wasi_path: str = "./iwasm",
    wasi_bin: str = ""
):
    results = {}
    
    if openai_api_key:
        print("\nTesting OpenAI API:")
        openai_result = get_openai_latency(
            api_key=openai_api_key,
            model=openai_model,
            prompt=prompt
        )
        results["openai"] = openai_result
        print(f"OpenAI Tokens: {openai_result['tokens']}")
        print(f"OpenAI Latency: {openai_result['latency']:.3f}s")
        if not openai_result['success']:
            print(f"OpenAI Error: {openai_result.get('error')}")
    
    print("\nTesting WASM SIMD:")
    try:
        start_time = time.time()
        wasm_tokens = get_llama32_wasm_simd(
            edge_model_path=wasm_path,
            edge_model_bin=wasm_bin,
            prompt=prompt
        )
        latency = time.time() - start_time
        results["wasm"] = {"tokens": wasm_tokens, "latency": latency, "success": True}
        print(f"WASM Tokens: {wasm_tokens}")
        print(f"WASM Latency: {latency:.3f}s")
    except Exception as e:
        print(f"WASM Error: {str(e)}")
        results["wasm"] = {"success": False, "error": str(e)}
    
    print("\nTesting WASI-NN:")
    try:
        start_time = time.time()
        wasi_tokens = get_llama_wasi_nn(
            edge_model_path=wasi_path,
            edge_model_bin=wasi_bin,
            prompt=prompt
        )
        latency = time.time() - start_time
        results["wasi"] = {"tokens": wasi_tokens, "latency": latency, "success": True}
        print(f"WASI Tokens: {wasi_tokens}")
        print(f"WASI Latency: {latency:.3f}s")
    except Exception as e:
        print(f"WASI-NN Error: {str(e)}")
        results["wasi"] = {"success": False, "error": str(e)}
    
    return results

if __name__ == "__main__":
    test_prompt = "Explain the concept of quantum computing in simple terms."
    
    # results = run_comparison_test(
    #     prompt=test_prompt,
    #     wasm_path="./MVVM_checkpoint",
    #     wasm_bin="./llama32_1b.bin",
    #     wasi_path="./iwasm"
    # )
    openai_result = get_openai_latency(
        api_key=os,
        model="gpt-3.5-turbo",
        prompt=test_prompt
    )
    print(openai_result)
    

    def plot_graph(results, labels):
        font = {'size': 40}
        plt.rc('font', **font)
        # Define colors for each platform
        colors = {
            'Wasm-SIMD': 'cyan',
            'Wasm-SIMD': 'blue',
            'WASI-NN-CPU': 'red',
            'WASI-NN-GPU': 'brown',
            'OpenAI': 'purple',
        }
        
        # Create figure and axis
        fig, ax = plt.subplots(figsize=(20, 10))
        
        # Create bar positions
        x = np.arange(len(results))
        
        # Create bars with specified colors
        bars = ax.bar(x, results, width=0.6)
        
        # Color each bar according to the platform
        for bar, label in zip(bars, labels):
            bar.set_color(colors.get(label, 'gray'))  # Default to gray if color not found
        
        # Customize the plot
        ax.set_ylabel('Performance Score')
        ax.set_xticks(x)
        ax.set_xticklabels(labels, fontsize=30)
        
        # Add value labels on top of each bar
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                    f'{height:.2f}',
                    ha='center', va='bottom')
        
        # Add grid for better readability
        ax.grid(True, axis='y', linestyle='--', alpha=0.7)
        
        # Adjust layout to prevent label cutoff
        plt.tight_layout()
        
        return plt

    # Example usage:
    results = [2.78, 3.15, 0.75, 0.84, 79.13263037306803]  # Example values
    platforms = ['Wasm', 'Wasm-SIMD', 'WASI-NN-CPU', 'WASI-NN-GPU', 'OpenAI']

    # Create and save the plot
    plot = plot_graph(results, platforms)
    plot.savefig('local_remote_gpt_tokens.pdf')
    plot.close()