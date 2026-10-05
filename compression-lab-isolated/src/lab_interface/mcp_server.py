"""Tool-only stdio MCP facade. No prompts, resources or owner baseline tables."""
from mcp.server.fastmcp import FastMCP
from .api import verify_runtime
from .profiling import capabilities
from compression_lab.util import Error


def serve(lab, *, port=None, token_file=None):
    verify_runtime()
    lab.environment()
    server = FastMCP("Compression Lab interface", instructions=None, log_level="WARNING", stateless_http=True)

    def call(function, *args, **kwargs):
        try:
            return function(*args, **kwargs)
        except (Error, OSError, ValueError) as error:
            # Exception filenames and owner paths are not part of the tool API.
            return {"status": "error", "code": getattr(error, "code", type(error).__name__)}

    @server.tool()
    def environment() -> dict:
        """Return verified input identities, the C ABI and development measurement definitions."""
        return call(lab.environment)

    @server.tool()
    def execute(argv: list[str]) -> dict:
        """Run a command in /work with /inputs and /interface read-only; network access is available."""
        return call(lab.execute, argv)

    @server.tool()
    def submit(manifest: str, request_id: str, quick: bool = False) -> dict:
        """Snapshot a candidate manifest under /work and queue byte-exact bulk/row measurements. Retry with the same request_id."""
        return call(lab.submit, manifest, request_id=request_id, quick=quick)

    @server.tool()
    def status(job_id: str) -> dict:
        """Read a durable job receipt. Queued jobs wait for the shared benchmark lock."""
        return call(lab.status, job_id)

    @server.tool()
    def result(result_id: str) -> dict:
        """Read a hash-verified result including the workspace's declared size and timing metrics."""
        return call(lab.result, result_id)

    @server.tool()
    def profiler_info() -> dict:
        """Read profiler availability and the precise diagnostic counter scope."""
        return {**capabilities(), 'measurement_location':'server' if lab.config.get('remote') else 'local',
                'remote_access':'A real server profile job tests hardware counter availability' if lab.config.get('remote') else None}

    @server.tool()
    def profile(result_id: str, request_id: str) -> dict:
        """Queue a separate perf diagnostic replay of a verified immutable candidate, never altering its timing result."""
        return call(lab.profile, result_id, request_id=request_id)

    @server.tool()
    def export(result_id: str) -> dict:
        """Export verified sources, binaries and raw evidence to a checksum-pinned ZIP under /exports."""
        return call(lab.export, result_id)

    @server.tool()
    def build(manifest: str, request_id: str) -> dict:
        """Freeze a source build manifest and run two clean reproducibility builds; returns a durable job."""
        return call(lab.build, manifest, request_id=request_id)

    @server.tool()
    def evaluate(build_id: str, request_id: str, quick: bool = False) -> dict:
        """Queue a reproducible build for pinned-core evaluation. Full uses one warmup and seven trials; quick is diagnostic."""
        return call(lab.evaluate, build_id, request_id=request_id, quick=quick)

    @server.tool()
    def evaluate_dbtext(build_id: str, request_id: str, quick: bool = False) -> dict:
        """Measure one row-capable package for both bulk and selected rows. Full: 7 bulk trials; 3 row replays with 100 warmup and 100 timed queries per column and selectivity. Quick cannot meet the target."""
        return call(lab.evaluate_dbtext, build_id, request_id=request_id, quick=quick)

    @server.tool()
    def validate(result_id: str, request_id: str) -> dict:
        """Queue independent memory/capacity and malformed-archive checks for a full source-built result."""
        return call(lab.validate, result_id, request_id=request_id)

    @server.tool()
    def jobs() -> dict:
        """List this workspace's durable jobs and their terminal or active states."""
        return call(lab.jobs)

    @server.tool()
    def cancel(job_id: str) -> dict:
        """Cancel this workspace's job, retaining evidence; never relaunches it."""
        return call(lab.cancel, job_id)

    @server.tool()
    def artifact(result_id: str, path: str | None = None, offset: int = 0, limit: int = 32768) -> dict:
        """List retained result files or read a bounded portion of one verified artifact."""
        return call(lab.artifact, result_id, path, offset=offset, limit=limit)

    @server.tool()
    def compare(result_ids: list[str]) -> dict:
        """Compare only this session's submitted measurements under the declared accounting policy."""
        return call(lab.compare, result_ids)

    @server.tool()
    def finish(result_id: str, validation_id: str, request_id: str) -> dict:
        """Preserve final source, build, full measurements and matching passed validation for owner review. A negative result is valid."""
        return call(lab.finish, result_id, validation_id, request_id=request_id)

    if port is None:
        server.run(transport="stdio")
    else:
        import hmac
        from pathlib import Path
        import uvicorn
        app=server.streamable_http_app()
        expected=('Bearer '+Path(token_file).read_text().strip()).encode()
        async def authenticated(scope,receive,send):
            if scope['type']=='http' and not hmac.compare_digest(dict(scope.get('headers',[])).get(b'authorization',b''),expected):
                await send({'type':'http.response.start','status':401,'headers':[]})
                await send({'type':'http.response.body','body':b'Unauthorized'})
                return
            await app(scope,receive,send)
        uvicorn.run(authenticated,host='127.0.0.1',port=port,log_level='warning')
