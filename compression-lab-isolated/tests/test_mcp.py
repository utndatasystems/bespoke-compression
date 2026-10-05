"""Exercise the real stdio transport with no model or provider calls."""
import asyncio
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

from lab_interface import PROJECT
from lab_interface.api import initialize

try:
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client
except ImportError:
    ClientSession = None


@unittest.skipIf(ClientSession is None, "Run with the project venv to test MCP")
class MCPTests(unittest.TestCase):
    def test_tools_without_prompts_baselines_or_owner_file_access(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            data = path / "strings"
            data.write_bytes(b"alpha\nbeta\n")
            lab = initialize(path / "lab", [data], host_lock=path / "host-benchmark.lock", cpu=min(os.sched_getaffinity(0)), server_local=os.environ.get("LAB_TEST_SERVER_LOCAL")=="1")
            secret = lab.owner / "baseline-table.json"
            secret.write_text('{"private_test_secret":123}')

            async def check():
                params = StdioServerParameters(command=sys.executable,
                    args=[str(PROJECT / "lab"), "--workspace", str(lab.root), "mcp"],
                    env={"PATH": "/usr/bin:/bin"})
                async with stdio_client(params) as (read, write):
                    async with ClientSession(read, write) as client:
                        handshake = await client.initialize()
                        self.assertFalse(handshake.instructions)
                        names = {t.name for t in (await client.list_tools()).tools}
                        self.assertEqual(names, {"environment", "execute", "submit", "status", "result", "profile", "export", "profiler_info",
                                                "build","evaluate","evaluate_dbtext","validate","jobs","cancel","artifact","compare","finish"})
                        self.assertEqual((await client.list_prompts()).prompts, [])
                        response = await client.call_tool("environment", {})
                        self.assertFalse(response.isError)
                        text = json.dumps(response.model_dump())
                        self.assertIn("pending_owner_decision", text)
                        self.assertNotIn("private_test_secret", text)
                        response = await client.call_tool("execute", {"argv": ["cat", str(secret)]})
                        self.assertNotIn("private_test_secret", json.dumps(response.model_dump()))
                        self.assertIn("No such file", json.dumps(response.model_dump()))
            asyncio.run(check())
