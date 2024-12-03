import os
import subprocess
from typing import Dict, Union, Optional
import time
import openai

def get_llama32_wasm_simd(edge_model_path="./MVVM_checkpoint",edge_model_bin="./llama32_1b.bin", prompt="Hello, world!"):
    cmd = [
        edge_model_path,
        "-t", "./llama.aot",
        "-a", f"{edge_model_bin},-i,\'{prompt}\'"
    ]
    print(cmd)
    
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=30  # 设置超时时间
    )
    
    if result.returncode != 0:
        raise Exception(f"Edge model error: {result.stderr}")
        
    res= result.stdout
    tokens = 0
    for r in res.split("\n"):
        print(r)
        if r.startswith("Token"):
            tokens = int(r.replace("s","").replace("Token",""))
    return tokens

def get_llama_wasi_nn(edge_model_path="./iwasm",edge_model_bin="", prompt="Hello, world!"):
    cmd = [
        edge_model_path, "./",
        f"{edge_model_bin},-i,\'{prompt}\'"
    ]
    print(cmd)
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=30  # 设置超时时间
    )
    # print(result.stdout)
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
    """
    Test OpenAI API latency with similar parameters to the WASM/WASI implementations.
    
    Args:
        api_key: OpenAI API key
        model: Model to use (default: "gpt-3.5-turbo")
        prompt: Input prompt (default: "Hello, world!")
        max_tokens: Maximum tokens to generate (default: 100)
        temperature: Sampling temperature (default: 0.7)
        
    Returns:
        Dictionary containing tokens generated and latency in seconds
    """
    client = openai.OpenAI(api_key=api_key)
    
    start_time = time.time()
    
    try:
        response = client.chat.completions.create(
            model=model,
            messages=[{"role": "user", "content": prompt}],
            max_tokens=max_tokens,
            temperature=temperature,
            timeout=30  # Matching the timeout in WASM/WASI implementations
        )
        
        end_time = time.time()
        latency = end_time - start_time
        
        # Get token count from response
        tokens_generated = response.usage.completion_tokens
        
        return {
            "tokens": tokens_generated,
            "latency": latency,
            "success": True
        }
        
    except Exception as e:
        end_time = time.time()
        return {
            "tokens": 0,
            "latency": end_time - start_time,
            "success": False,
            "error": str(e)
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
    """
    Run a comparison test between OpenAI API, WASM, and WASI-NN implementations
    """
    # Test OpenAI
    if openai_api_key:
        print("\nTesting OpenAI API:")
        openai_result = get_openai_latency(
            api_key=openai_api_key,
            model=openai_model,
            prompt=prompt
        )
        print(f"OpenAI Tokens: {openai_result['tokens']}")
        print(f"OpenAI Latency: {openai_result['latency']:.3f}s")
        if not openai_result['success']:
            print(f"OpenAI Error: {openai_result.get('error')}")
    
    # Test WASM SIMD
    print("\nTesting WASM SIMD:")
    try:
        wasm_tokens = get_llama32_wasm_simd(
            edge_model_path=wasm_path,
            edge_model_bin=wasm_bin,
            prompt=prompt
        )
        print(f"WASM Tokens: {wasm_tokens}")
    except Exception as e:
        print(f"WASM Error: {str(e)}")
    
    # Test WASI-NN
    print("\nTesting WASI-NN:")
    try:
        get_llama_wasi_nn(
            edge_model_path=wasi_path,
            edge_model_bin=wasi_bin,
            prompt=prompt
        )
    except Exception as e:
        print(f"WASI-NN Error: {str(e)}")

# Example usage
if __name__ == "__main__":
    # Replace with your OpenAI API key
    OPENAI_API_KEY = "your-api-key-here"
    
    test_prompt = "Explain the concept of quantum computing in simple terms."
    
    run_comparison_test(
        prompt=test_prompt,
        openai_api_key=OPENAI_API_KEY,
        wasm_path="./MVVM_checkpoint",
        wasm_bin="./llama32_1b.bin",
        wasi_path="./iwasm"
    )