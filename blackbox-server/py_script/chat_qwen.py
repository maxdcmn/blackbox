#!/usr/bin/env python3
import json
import sys
import argparse
import requests
import time
import os
from typing import Optional, Dict, Any, List
from datetime import datetime

class ChatBot:
    def __init__(self, url: str, model: str = "Qwen/Qwen2.5-7B-Instruct", 
                 temperature: float = 0.7, max_tokens: Optional[int] = None,
                 system_prompt: Optional[str] = None):
        self.url = url.rstrip('/').split('/vram')[0].split('/v1')[0]
        self.endpoint = f"{self.url}/v1/chat/completions"
        self.model = model
        self.temperature = temperature
        self.max_tokens = max_tokens
        self.messages: List[Dict[str, str]] = []
        if system_prompt:
            self.messages.append({"role": "system", "content": system_prompt})
    
    def chat(self, user_input: str, stream: bool = True) -> Optional[str]:
        """Send a message and get response."""
        self.messages.append({"role": "user", "content": user_input})
        
        payload = {
            "model": self.model,
            "messages": self.messages,
            "temperature": self.temperature,
            "stream": stream
        }
        
        if self.max_tokens:
            payload["max_tokens"] = self.max_tokens
        
        try:
            if stream:
                response = requests.post(self.endpoint, json=payload, stream=True, timeout=120)
                response.raise_for_status()
                
                assistant_content = ""
                for line in response.iter_lines(decode_unicode=True):
                    if line:
                        if line.startswith("data: "):
                            data_str = line[6:]
                            if data_str.strip() == "[DONE]":
                                break
                            try:
                                data = json.loads(data_str)
                                if "choices" in data and len(data["choices"]) > 0:
                                    delta = data["choices"][0].get("delta", {})
                                    if "content" in delta:
                                        content = delta["content"]
                                        assistant_content += content
                                        print(content, end="", flush=True)
                            except json.JSONDecodeError:
                                continue
                print()  # Newline after streaming
                
                if assistant_content:
                    self.messages.append({"role": "assistant", "content": assistant_content})
                return assistant_content
            else:
                response = requests.post(self.endpoint, json=payload, timeout=120)
                response.raise_for_status()
                result = response.json()
                
                if "choices" in result and len(result["choices"]) > 0:
                    assistant_content = result["choices"][0]["message"]["content"]
                    self.messages.append({"role": "assistant", "content": assistant_content})
                    return assistant_content
                return None
        except requests.exceptions.RequestException as e:
            print(f"\nError: {e}", file=sys.stderr)
            return None
    
    def clear_history(self):
        """Clear conversation history (keep system prompt if any)."""
        system_msg = None
        if self.messages and self.messages[0].get("role") == "system":
            system_msg = self.messages[0]
        self.messages = []
        if system_msg:
            self.messages.append(system_msg)
    
    def get_history(self) -> List[Dict[str, str]]:
        """Get conversation history."""
        return self.messages.copy()

def print_header(bot: ChatBot):
    """Print chatbot header."""
    print("\n" + "=" * 70)
    print(f"  Qwen ChatBot - {bot.model}")
    print(f"  Server: {bot.url}")
    print("=" * 70)
    print("Commands: /clear, /history, /quit, /exit, /help")
    print("-" * 70 + "\n")

def print_help():
    """Print help message."""
    print("\nCommands:")
    print("  /clear     - Clear conversation history")
    print("  /history   - Show conversation history")
    print("  /quit      - Exit chatbot")
    print("  /exit      - Exit chatbot")
    print("  /help      - Show this help")
    print()

def main():
    parser = argparse.ArgumentParser(
        description='CLI Chatbot for Qwen model via vLLM',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                          # Start interactive chatbot
  %(prog)s --url http://64.176.192.233:8000
  %(prog)s --prompt "Hello"          # Single message
  %(prog)s --system "You are a helpful assistant"
        """
    )
    parser.add_argument('--url', default='http://localhost:8000',
                       help='vLLM server URL (default: http://localhost:8000)')
    parser.add_argument('--model', default='Qwen/Qwen2.5-7B-Instruct',
                       help='Model name (default: Qwen/Qwen2.5-7B-Instruct)')
    parser.add_argument('--prompt', '-p', type=str,
                       help='Single prompt message (non-interactive)')
    parser.add_argument('--system', type=str,
                       help='System prompt to set behavior')
    parser.add_argument('--no-stream', action='store_true',
                       help='Disable streaming (slower but shows full response at once)')
    parser.add_argument('--temperature', '-t', type=float, default=0.7,
                       help='Temperature (default: 0.7)')
    parser.add_argument('--max-tokens', type=int,
                       help='Maximum tokens to generate')
    parser.add_argument('--loop', '-l', action='store_true',
                       help='Loop mode: send test message every 5 seconds')
    
    args = parser.parse_args()
    
    bot = ChatBot(
        url=args.url,
        model=args.model,
        temperature=args.temperature,
        max_tokens=args.max_tokens,
        system_prompt=args.system
    )
    
    if args.loop:
        print("Loop mode: sending test message every 5 seconds (Ctrl+C to stop)")
        try:
            while True:
                print(f"\n[{datetime.now().strftime('%H:%M:%S')}] Sending request...")
                response = bot.chat("Hello, testing inference", stream=not args.no_stream)
                if response and args.no_stream:
                    print(f"Response: {response[:100]}...")
                time.sleep(5)
        except KeyboardInterrupt:
            print("\nStopped.")
    elif args.prompt:
        # Single message mode
        response = bot.chat(args.prompt, stream=not args.no_stream)
        if response and args.no_stream:
            print(response)
    else:
        # Interactive chatbot mode
        print_header(bot)
        
        try:
            while True:
                try:
                    user_input = input("You: ").strip()
                    
                    if not user_input:
                        continue
                    
                    # Handle commands
                    if user_input.startswith('/'):
                        cmd = user_input.lower().split()[0]
                        if cmd in ['/quit', '/exit', '/q']:
                            print("\nGoodbye!\n")
                            break
                        elif cmd == '/clear':
                            bot.clear_history()
                            print("Conversation history cleared.\n")
                            continue
                        elif cmd == '/history':
                            history = bot.get_history()
                            print(f"\nConversation History ({len(history)} messages):")
                            print("-" * 70)
                            for i, msg in enumerate(history, 1):
                                role = msg.get("role", "unknown")
                                content = msg.get("content", "")[:200]
                                print(f"{i}. {role.upper()}: {content}...")
                            print("-" * 70 + "\n")
                            continue
                        elif cmd == '/help':
                            print_help()
                            continue
                        else:
                            print(f"Unknown command: {cmd}. Type /help for commands.\n")
                            continue
                    
                    # Send message
                    print("Assistant: ", end="", flush=True)
                    bot.chat(user_input, stream=not args.no_stream)
                    print()  # Extra newline for readability
                    
                except EOFError:
                    print("\nGoodbye!\n")
                    break
                except KeyboardInterrupt:
                    print("\n\nInterrupted. Type /quit to exit or continue chatting.\n")
                    continue
        except KeyboardInterrupt:
            print("\n\nExiting...\n")

if __name__ == '__main__':
    main()

