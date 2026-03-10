"""TCP connection to the UnrealAgent UE plugin."""

import asyncio
import json
import logging

logger = logging.getLogger(__name__)


class UnrealConnection:
    """Manages the TCP connection to the UnrealAgent plugin's TCP server."""

    def __init__(self, host: str = "127.0.0.1", port: int = 55557):
        self.host = host
        self.port = port
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None
        self._lock = asyncio.Lock()
        self._request_id = 0

    async def connect(self, timeout: float = 5.0) -> bool:
        """Establish connection to the UE plugin TCP server."""
        try:
            self.reader, self.writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=timeout,
            )
            logger.info(f"Connected to UnrealAgent at {self.host}:{self.port}")
            return True
        except (ConnectionRefusedError, OSError, asyncio.TimeoutError) as e:
            logger.error(f"Failed to connect to {self.host}:{self.port}: {e}")
            return False

    async def disconnect(self):
        """Close the connection."""
        if self.writer:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass
            self.writer = None
            self.reader = None
            logger.info("Disconnected from UnrealAgent")

    async def ensure_connected(self) -> bool:
        """Ensure we have an active connection, reconnecting if necessary."""
        if self.writer is not None and not self.writer.is_closing():
            return True
        return await self.connect()

    async def send_request(self, method: str, params: dict | None = None) -> dict:
        """Send a JSON-RPC request and wait for the response.

        Args:
            method: The JSON-RPC method name (e.g., "get_project_info")
            params: Optional parameters dict

        Returns:
            The result dict from the JSON-RPC response

        Raises:
            ConnectionError: If not connected
            RuntimeError: If the server returns an error
        """
        async with self._lock:
            if not await self.ensure_connected():
                raise ConnectionError(
                    f"Cannot connect to UnrealAgent at {self.host}:{self.port}. "
                    "Make sure the Unreal Editor is running with the UnrealAgent plugin."
                )

            self._request_id += 1
            request = {
                "jsonrpc": "2.0",
                "method": method,
                "params": params or {},
                "id": self._request_id,
            }

            # Send with Content-Length framing
            payload = json.dumps(request).encode("utf-8")
            header = f"Content-Length: {len(payload)}\r\n\r\n".encode("utf-8")

            try:
                self.writer.write(header + payload)
                await self.writer.drain()
            except (ConnectionError, OSError) as e:
                await self.disconnect()
                raise ConnectionError(f"Failed to send request: {e}")

            # Read response with Content-Length framing
            try:
                response_data = await self._read_response()
            except (ConnectionError, OSError, asyncio.IncompleteReadError) as e:
                await self.disconnect()
                raise ConnectionError(f"Failed to read response: {e}")

            response = json.loads(response_data)

            # Check for errors
            if "error" in response:
                error = response["error"]
                raise RuntimeError(
                    f"UnrealAgent error [{error.get('code', 'unknown')}]: "
                    f"{error.get('message', 'Unknown error')}"
                )

            return response.get("result", {})

    async def _read_response(self) -> bytes:
        """Read a Content-Length framed response."""
        # Read headers until we find Content-Length
        content_length = None
        while True:
            line = await self.reader.readline()
            if not line:
                raise ConnectionError("Connection closed by server")

            line_str = line.decode("utf-8").strip()

            if line_str == "":
                # Empty line = end of headers
                if content_length is not None:
                    break
                continue

            if line_str.lower().startswith("content-length:"):
                content_length = int(line_str.split(":")[1].strip())

        if content_length is None:
            raise RuntimeError("No Content-Length header in response")

        # Read the payload
        data = await self.reader.readexactly(content_length)
        return data
