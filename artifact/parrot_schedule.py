# 96 server 1 common prompt and snapshot
import os
from datetime import datetime, timedelta
from typing import Dict, List, Union, Literal
import openai
from dataclasses import dataclass
import json
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
                model="gpt-4",
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

    def _call_edge_model(self, prompt: str) -> str:
        """
        Call the edge model using command line
        """
        try:
            cmd = [
                self.edge_model_path,
                "-t", "./llama.aot",
                "-a", f"{self.edge_model_bin},-i,\'{prompt}\'"
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
                
            return result.stdout
            
        except subprocess.TimeoutExpired:
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

    def process_request(self, user_request: str) -> ModelResponse:
        """
        Process user request using either cloud or edge model
        """
        import time
        start_time = time.time()
        
        try:
            # 决定使用哪个模型
            model_type = self._route_request(user_request)
            
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

# Example usage
if __name__ == "__main__":
    # 初始化agent
    agent = IntelligentAgent(
        api_key=os.environ["OPENAI_API_KEY"],
        edge_model_path="./MVVM_checkpoint",
        edge_model_bin="./llama32_1b.bin"
    )
    
    # 测试不同复杂度的请求
    test_requests = [
        "What is 2+2?",  # 简单计算,适合边缘模型
        "Write a detailed analysis of the economic impact of climate change， please use scraper to do so",  # 复杂分析,需要云端模型
        "Tell me a joke",  # 简单生成,适合边缘模型
        "Explain quantum computing to a 5 year old"  # 需要深度解释,可能需要云端模型
    ]
    
    for request in test_requests:
        print(f"\nProcessing request: {request}")
        response = agent.process_request(request)
        print(f"Used {response.model_used.value} model")
        print(f"Processing time: {response.processing_time:.2f}s")
        if response.error:
            print(f"Error: {response.error}")
        else:
            print(f"Response: {response.content[:100]}...")  # 只显示前100个字符