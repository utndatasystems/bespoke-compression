"""CLI for the same narrow methods exposed over MCP."""
import argparse
import json
import os
from pathlib import Path
import sys

from . import PROJECT
from .api import Lab, _clean_env, initialize, verify_runtime
from .profiling import capabilities
from compression_lab import runner
from compression_lab.util import Error


def main():
    parser = argparse.ArgumentParser(description="Isolated Compression Lab development interface; scoring pending.")
    parser.add_argument("--workspace", type=Path)
    commands = parser.add_subparsers(dest="command", required=True)
    init = commands.add_parser("init", help="Owner only: create a new workspace, without running candidates")
    init.add_argument("--inputs", type=Path, nargs="+", required=True)
    init.add_argument("--cpu", type=int, required=True)
    init.add_argument("--row-framing", choices=("lf", "nul", "none"), default="lf")
    init.add_argument("--host-lock", type=Path)
    init.add_argument("--server-local", action="store_true", help="Use PRoot paths and mandatory Landlock/seccomp when user namespaces are disabled")
    commands.add_parser("doctor", help="Read-only capability checks")
    commands.add_parser("environment")
    commands.add_parser("profiler-info")
    commands.add_parser("mcp", help="Serve only over stdio; does not launch a model")
    http=commands.add_parser('mcp-http',help='Owner-only authenticated localhost HTTP transport')
    http.add_argument('--port',type=int,required=True)
    http.add_argument('--token-file',type=Path,required=True)
    commands.add_parser("mcp-config", help="Print the owner-side MCP client configuration")
    submit = commands.add_parser("submit")
    submit.add_argument("manifest")
    submit.add_argument("--request-id", required=True)
    submit.add_argument("--quick", action="store_true")
    profile = commands.add_parser("profile")
    profile.add_argument("result_id")
    profile.add_argument("--request-id", required=True)
    build=commands.add_parser('build');build.add_argument('manifest');build.add_argument('--request-id',required=True)
    evaluate=commands.add_parser('evaluate');evaluate.add_argument('build_id');evaluate.add_argument('--request-id',required=True);evaluate.add_argument('--quick',action='store_true')
    validate=commands.add_parser('validate');validate.add_argument('result_id');validate.add_argument('--request-id',required=True)
    commands.add_parser('jobs')
    commands.add_parser('cancel').add_argument('job_id')
    compare=commands.add_parser('compare');compare.add_argument('result_ids',nargs='+')
    artifact=commands.add_parser('artifact');artifact.add_argument('result_id');artifact.add_argument('path',nargs='?')
    finish=commands.add_parser('finish');finish.add_argument('result_id');finish.add_argument('validation_id');finish.add_argument('--request-id',required=True)
    for command in ("status", "result", "export"):
        commands.add_parser(command).add_argument("id")
    for command in ("execute", "shell"):
        p = commands.add_parser(command)
        p.add_argument("argv", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        if args.command == "doctor":
            probe = runner.probe()
            response = {"runtime": verify_runtime(), "allowed_cpus": sorted(os.sched_getaffinity(0)),
                        "measurement_sandbox": {k: probe[k] for k in ("status", "checks") if k in probe},
                        "profiler": capabilities()}
        elif args.command == "profiler-info":
            response = capabilities()
        else:
            if not args.workspace:
                parser.error("--workspace is required for this command")
            if args.command == "init":
                lab = initialize(args.workspace, args.inputs, cpu=args.cpu, row_framing=args.row_framing,
                                 host_lock=args.host_lock, server_local=args.server_local)
                response = lab.environment()
            elif args.command == "mcp-config":
                response = {"mcpServers": {"compression_lab": {"command": sys.executable,
                           "args": [str(PROJECT / "lab"), "--workspace", str(args.workspace.resolve()), "mcp"]}}}
            else:
                lab = Lab(args.workspace)
                if args.command in ("mcp",'mcp-http'):
                    from .mcp_server import serve
                    serve(lab,**({'port':args.port,'token_file':args.token_file} if args.command=='mcp-http' else {}))
                    return
                if args.command == "environment":
                    response = lab.environment()
                elif args.command == "submit":
                    response = lab.submit(args.manifest, request_id=args.request_id, quick=args.quick)
                elif args.command == "profile":
                    response = lab.profile(args.result_id, request_id=args.request_id)
                elif args.command=='build':response=lab.build(args.manifest,request_id=args.request_id)
                elif args.command=='evaluate':response=lab.evaluate(args.build_id,request_id=args.request_id,quick=args.quick)
                elif args.command=='validate':response=lab.validate(args.result_id,request_id=args.request_id)
                elif args.command=='jobs':response=lab.jobs()
                elif args.command=='cancel':response=lab.cancel(args.job_id)
                elif args.command=='compare':response=lab.compare(args.result_ids)
                elif args.command=='artifact':response=lab.artifact(args.result_id,args.path)
                elif args.command=='finish':response=lab.finish(args.result_id,args.validation_id,request_id=args.request_id)
                elif args.command in ("execute", "shell"):
                    argv = args.argv[1:] if args.argv[:1] == ["--"] else args.argv
                    if args.command == "shell":
                        command = lab.shell_command(argv or ["/bin/bash"])
                        os.execve(command[0], command, _clean_env())
                    response = lab.execute(argv)
                else:
                    response = getattr(lab, args.command)(args.id)
        print(json.dumps(response, indent=2, allow_nan=False))
    except (Error, OSError, ValueError) as error:
        print(json.dumps({"status": "error", "code": getattr(error, "code", type(error).__name__)}))
        sys.exit(1)
