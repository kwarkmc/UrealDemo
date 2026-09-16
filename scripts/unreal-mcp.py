"""Small client for the engine's local MCP server, using the Python standard library."""
import argparse
import json
from pathlib import Path
from urllib.request import Request, build_opener, ProxyHandler


def call(name, arguments):
    url = "http://127.0.0.1:8000/mcp"
    opener = build_opener(ProxyHandler({}))
    headers = {"Content-Type": "application/json", "Accept": "application/json",
               "Mcp-Protocol-Version": "2025-11-25"}

    def send(method, params, request_id=None):
        body = {"jsonrpc": "2.0", "method": method, "params": params}
        if request_id is not None:
            body["id"] = request_id
        with opener.open(Request(url, json.dumps(body).encode(), headers), timeout=60) as response:
            if response.headers.get("Mcp-Session-Id"):
                headers["Mcp-Session-Id"] = response.headers["Mcp-Session-Id"]
            data = response.read()
        value = json.loads(data) if data else {}
        if "error" in value:
            raise RuntimeError(value["error"])
        return value.get("result", value)

    send("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                        "clientInfo": {"name": "UAVSim Harness", "version": "0.1"}}, 1)
    try:
        send("notifications/initialized", {})
        return send("tools/call", {"name": name, "arguments": arguments}, 2)
    finally:
        with opener.open(Request(url, headers=headers, method="DELETE"), timeout=10):
            pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", nargs="?", default="list_toolsets")
    parser.add_argument("--arguments-file", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    arguments = json.loads(args.arguments_file.read_text(encoding="utf-8-sig")) if args.arguments_file else {}
    result = call(args.name, arguments)
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
        print(args.output)
    else:
        print(text)
    if result.get("isError"):
        raise SystemExit(1)
