import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";

const server = new Server(
  {
    name: "dsp-accel-monitor",
    version: "1.0.0",
  },
  {
    capabilities: {
      tools: {},
    },
  }
);

import fs from "fs";

/**
 * Helper to read live hardware telemetry from the Supervisor's state dump.
 */
function getLiveStatus() {
  try {
    const data = fs.readFileSync("/run/dsp_accel/monitor.json", "utf8");
    return JSON.parse(data);
  } catch (e) {
    return { workers: [] };
  }
}

/**
 * Tool definitions for the DSP Acceleration System.
 */
server.setRequestHandler(ListToolsRequestSchema, async () => {
  return {
    tools: [
      {
        name: "list_dsp_workers",
        description: "List all active hardware workers (GPU, NPU, DSP) and their real-time load/health.",
        inputSchema: { type: "object", properties: {} },
      },
      {
        name: "get_worker_health",
        description: "Get detailed health snapshot and heartbeat for a specific worker.",
        inputSchema: {
          type: "object",
          properties: {
            worker_id: { type: "integer", description: "The ID of the worker (0-7)" },
          },
          required: ["worker_id"],
        },
      },
    ],
  };
});

/**
 * Execute tool logic using live data from the daemon.
 */
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;
  const status = getLiveStatus();

  switch (name) {
    case "list_dsp_workers":
      return {
        content: [{ type: "text", text: JSON.stringify(status.workers, null, 2) }],
      };

    case "get_worker_health":
      const worker = status.workers.find(w => w.id === args.worker_id);
      if (!worker) {
        return { content: [{ type: "text", text: `Worker ${args.worker_id} not found.` }] };
      }
      return {
        content: [{ type: "text", text: `Worker ${worker.id} (${worker.type}): ${worker.status}, Load: ${worker.load}%, Heartbeat: ${worker.heartbeat}` }],
      };

    default:
      throw new Error("Tool not found");
  }
});

const transport = new StdioServerTransport();
await server.connect(transport);
console.error("[DSP MCP] Server started via Stdio.");
