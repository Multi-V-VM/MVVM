# 96 server 1 common prompt and snapshot
import os
from datetime import datetime, timedelta
from typing import Dict, List, Union, Literal
import openai
from dataclasses import dataclass
import json
import signal
import subprocess
from enum import Enum
import asyncio

class ModelType(Enum):
    EDGE = "edge"
    CLOUD = "cloud"

@dataclass
class ModelResponse:
    content: str
    model_used: ModelType
    processing_time: float
    error: str = None

class LLMScheduler:
    def __init__(self):
        self.latency_requirements = {}

    def schedule_requests_with_llm(self, requests: List[str]) -> List[str]:
        """
        Use LLM to prioritize requests and return the ordered list.
        """
        try:
            # Build the prompt
            system_prompt = """
            You are a scheduling agent for a latency-sensitive system. Your task is to reorder a list of requests 
            to optimize system performance based on the following criteria:
            
            1. Short latency tasks (e.g., simple queries or calculations) should be prioritized.
            2. Medium latency tasks (e.g., summaries or translations) come next.
            3. High latency tasks (e.g., creative writing or complex analysis) are handled last.
            
            Here is the list of requests:
            {requests}
            
            Return the indices of the requests in the optimal order for scheduling, starting with 1. Example: "1,2,4,3"
            """
            formatted_prompt = system_prompt.format(requests="\n".join([f"{i+1}. {req}" for i, req in enumerate(requests)]))

            # Call the LLM
            response = openai.ChatCompletion.create(
                model="gpt-4o",
                messages=[
                    {"role": "system", "content": formatted_prompt}
                ],
                temperature=0,
                max_tokens=20
            )

            # Extract the response and convert it to a list of integers
            decision = response.choices[0].message.content.strip()
            order = list(map(int, decision.split(',')))

            # Reorder requests based on LLM's decision
            sorted_requests = [requests[i - 1] for i in order]
            return sorted_requests

        except Exception as e:
            print(f"Scheduling failed: {e}, returning requests in original order.")
            return requests  # Fallback to original order
        
class IntelligentAgent:
    def __init__(self, api_key: str, edge_model_path: str, edge_model_bin: str):
        """
        Initialize the agent with both cloud and edge model configurations
        
        Args:
            api_key: OpenAI API key for cloud model
            edge_model_path: Path to edge model executable
            edge_model_bin: Path to edge model binary file
        """
        self.api_key = api_key
        openai.api_key = api_key
        self.edge_model_path = edge_model_path
        self.edge_model_bin = edge_model_bin
        
    def _route_request(self, user_request: str) -> ModelType:
        """
        Determine whether to use cloud or edge model based on request complexity
        """
        # 使用 GPT 判断任务复杂度
        system_prompt = """
        You are a routing agent. Analyze the user request and determine if it needs a powerful cloud model (GPT-4) 
        or if a smaller edge model (1B parameters) would suffice. Consider:
        
        1. Task complexity
        2. Required reasoning depth
        3. Need for external knowledge
        4. Creative generation needs
        
        Return only "edge" or "cloud" as your decision.
        """
        
        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_request}
        ]
        
        try:
            response = openai.ChatCompletion.create(
                model="gpt-4o",
                messages=messages,
                temperature=0,
                max_tokens=10
            )
            decision = response.choices[0].message.content.strip().lower()
            return ModelType.CLOUD if decision == "cloud" else ModelType.EDGE
        except Exception as e:
            # 如果路由决策失败,默认使用边缘模型
            print(f"Routing decision failed: {e}, defaulting to edge model")
            return ModelType.EDGE

    def process_requests(requests: List[str]):
        scheduler = Scheduler()
        sorted_requests = scheduler.schedule_requests(requests)

        results = {}
        for idx, request in enumerate(sorted_requests):
            # Route each request
            model_type = _route_request(request)

            # Process the request
            if model_type == "cloud":
                results[request] = f"Processed on Cloud Model (Request {idx + 1})"
            else:
                results[request] = f"Processed on Edge Model (Request {idx + 1})"

        return results

    async def _call_edge_model_async(self, prompt: str) -> str:
        """
        Call the edge model asynchronously using command line and pass input via stdin.
        """
        try:
            cmd = [
                self.edge_model_path,
                "./wasmedge-ggml-llama.aot",
                f"{self.edge_model_bin}"
            ]
            print(cmd)

            # Create the subprocess
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )

            # Send the prompt to stdin
            stdout, stderr = await process.communicate(input=prompt.encode())

            # Check for errors
            if process.returncode != 0:
                raise Exception(f"Edge model error: {stderr.decode()}")

            # Return the stdout as a string
            return stdout.decode()

        except asyncio.TimeoutError:
            raise Exception("Edge model timed out")
        except Exception as e:
            raise Exception(f"Edge model error: {str(e)}")


    async def _run_clangd_async(self, prompt: str) -> str:
        """
        Call the edge model asynchronously using command line and pass input via stdin.
        """
        try:
            cmd = [
                self.edge_model_path,
                "./clangd.aot",
            ]
            print(cmd)

            # Create the subprocess
            process = await asyncio.create_subprocess_exec(
                *cmd,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )

            # Send the prompt to stdin
            stdout, stderr = await process.communicate(input=prompt.encode())

            # Check for errors
            if process.returncode != 0:
                raise Exception(f"Edge model error: {stderr.decode()}")

            # Return the stdout as a string
            return stdout.decode()

        except asyncio.TimeoutError:
            raise Exception("Edge model timed out")
        except Exception as e:
            raise Exception(f"Edge model error: {str(e)}")

    def _call_cloud_model(self, prompt: str) -> str:
        """
        Call the cloud model (GPT-4) using OpenAI API
        """
        try:
            response = openai.ChatCompletion.create(
                model="gpt-4",
                messages=[
                    {"role": "user", "content": prompt}
                ],
                temperature=0.7
            )
            return response.choices[0].message.content
            
        except Exception as e:
            raise Exception(f"Cloud model error: {str(e)}")

    def process_request(self, user_request: str, use_edge: bool = False) -> ModelResponse:
        """
        Process user request using either cloud or edge model
        """
        import time
        start_time = time.time()
        
        try:
            # 决定使用哪个模型
            model_type = ModelType.EDGE if use_edge else self._route_request(user_request)
            
            # 根据路由决策调用相应模型
            if model_type == ModelType.CLOUD:
                response = self._call_cloud_model(user_request)
            else:
                response = self._call_edge_model(user_request)
            
            processing_time = time.time() - start_time
            
            return ModelResponse(
                content=response,
                model_used=model_type,
                processing_time=processing_time
            )
            
        except Exception as e:
            return ModelResponse(
                content=None,
                model_used=model_type if 'model_type' in locals() else ModelType.EDGE,
                processing_time=time.time() - start_time,
                error=str(e)
            )
    def control_c(self):
        if self.process and self.process.pid:
            os.kill(self.process.pid, signal.SIGINT)
            print(f"Sent Control+C to subprocess (PID: {self.process.pid})")
        else:
            print("No subprocess to send Control+C to.")

# Example usage
async def main():
    # 初始化agent
    agent = IntelligentAgent(
        api_key=os.environ["OPENAI_API_KEY"],
        edge_model_path="/mnt/osdi23/MVVM/build/wasm-micro-runtime/product-mini/platforms/linux/build/iwasm",
        edge_model_bin="./Llama-3.2-1B-Instruct-Q8_0.gguf"
    )
    
    # 测试不同复杂度的请求
    test_requests = [
        "What is 2+2?, verify in c++", 
        "Generate code for a simple web server in c++", 
        "Tell me a c++ joke", 
        "Explain quantum computing in c++" 
    ]
    
    for request in test_requests:
        print(f"\nProcessing request: {request}")
        response = agent.process_request(request, use_edge=True)
        print(f"Used {response.model_used.value} model")
        print(f"Processing time: {response.processing_time:.2f}s")
        if response.error:
            print(f"Error: {response.error}")
        else:
            print(f"Response: {response.content[:100]}...")  # 只显示前100个字符

    # 测试不同复杂度的请求
    test_requests = [
        "What is 2+2?, verify in c++", 
        "Tell me a c++ joke", 
        "Generate code for a simple web server in c++", 
        "Explain quantum computing in c++" 
    ]
    
    for request in test_requests:
        print(f"\nProcessing request: {request}")
        response = agent.process_request(request, use_edge=False)
        print(f"Used {response.model_used.value} model")
        print(f"Processing time: {response.processing_time:.2f}s")
        if response.error:
            print(f"Error: {response.error}")
        else:
            print(f"Response: {response.content[:100]}...")  # 只显示前100个字符


    normal_request = "What is 2+2?, verify in c++"
    test_requests = [
        "what's 2+2",
        "generate code for a simple web server",
        "tell me a joke",
        "explain quantum computing"
    ]
    task = asyncio.create_task(agent._call_edge_model_async(normal_request))
    
    # Simulate sending Control+C after 5 seconds
    await asyncio.sleep(5)
    agent.control_c()

    for request in test_requests:
        print(f"\nProcessing request: {request}")
        response = agent.process_request(request, use_edge=False)
        print(f"Used {response.model_used.value} model")
        print(f"Processing time: {response.processing_time:.2f}s")
        if response.error:
            print(f"Error: {response.error}")
        else:
            print(f"Response: {response.content[:100]}...")  # 只显示前100个字符
    
