const stateUrl = "/api/state";
const writeCommands = new Set(["insert", "delete", "bold", "italic", "heading", "newline"]);
let currentState = null;
const commandMeta = {
  get: {
    commandHint: "Fetch the latest snapshot for the selected user.",
    posHint: "No position is needed for get.",
    showLength: false,
    lengthHint: "No length is needed for get.",
    payloadPlaceholder: "No payload needed",
    disablePayload: true,
  },
  insert: {
    commandHint: "Insert the payload at the given position. Length is not used for insert.",
    posHint: "If position is past the current document length, the backend appends the payload at the end.",
    showLength: false,
    lengthHint: "Length is ignored for insert.",
    payloadPlaceholder: "Text to insert",
    disablePayload: false,
  },
  delete: {
    commandHint: "Delete a range starting at position with the given length.",
    posHint: "Start index for the deletion.",
    showLength: true,
    lengthLabel: "Length",
    lengthHint: "Number of characters to delete.",
    payloadPlaceholder: "No payload needed",
    disablePayload: true,
  },
  bold: {
    commandHint: "Wrap the range from position to end with bold markdown.",
    posHint: "Start index for bold formatting.",
    showLength: true,
    lengthLabel: "End",
    lengthHint: "End index for the formatted range.",
    payloadPlaceholder: "No payload needed",
    disablePayload: true,
  },
  italic: {
    commandHint: "Wrap the range from position to end with italic markdown.",
    posHint: "Start index for italic formatting.",
    showLength: true,
    lengthLabel: "End",
    lengthHint: "End index for the formatted range.",
    payloadPlaceholder: "No payload needed",
    disablePayload: true,
  },
  heading: {
    commandHint: "Insert a markdown heading prefix at the given position.",
    posHint: "Position where the heading marker should be inserted.",
    showLength: true,
    lengthLabel: "Level",
    lengthHint: "Heading level from 1 to 6.",
    payloadPlaceholder: "No payload needed",
    disablePayload: true,
  },
  newline: {
    commandHint: "Insert a newline at the given position.",
    posHint: "Position where the newline should be inserted.",
    showLength: false,
    lengthHint: "Length is not used for newline.",
    payloadPlaceholder: "No payload needed",
    disablePayload: true,
  },
};

async function request(path, options = {}) {
  const response = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  return response.json();
}

function el(id) {
  return document.getElementById(id);
}

function syncProtocolWarning() {
  el("protocol-warning").hidden = window.location.protocol !== "file:";
}

function selectedUserRole() {
  if (!currentState) {
    return null;
  }

  const username = el("username").value;
  const session = currentState.sessions.find((entry) => entry.username === username);
  return session?.role ?? null;
}

function updateUserOptions() {
  const select = el("username");
  const existing = new Set(Array.from(select.options).map((option) => option.value));
  const roles = currentState?.roles ?? [];

  for (const entry of roles) {
    if (existing.has(entry.username)) {
      continue;
    }
    const option = document.createElement("option");
    option.value = entry.username;
    option.textContent = entry.username;
    select.appendChild(option);
  }
}

function setFeedback(message, isError = false) {
  const node = el("action-feedback");
  node.textContent = message;
  node.className = isError ? "session-error" : "panel-copy";
}

function parseNumber(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : 0;
}

function applySortedInserts(base, inserts) {
  const sorted = [...inserts].sort((left, right) => left.pos - right.pos);
  let text = base;
  let offset = 0;

  for (const insert of sorted) {
    const oldLen = text.length;
    let pos = insert.pos;
    if (pos + offset > oldLen) {
      pos = oldLen - offset;
    }
    const effectivePos = Math.max(0, pos + offset);
    text = text.slice(0, effectivePos) + insert.text + text.slice(effectivePos);
    offset += insert.text.length;
  }

  return text;
}

function computeEffectiveOperation() {
  const snapshot = currentState?.snapshot ?? { document: "" };
  const base = snapshot.document ?? "";
  const command = el("command").value;
  const pos = parseNumber(el("pos").value);
  const length = parseNumber(el("length").value);
  const payload = el("payload").value;

  if (command === "get") {
    return {
      summary: "Fetches the latest snapshot without modifying the document.",
      preview: base || "(empty document)",
    };
  }

  if (command === "insert") {
    const effectivePos = Math.min(pos, base.length);
    return {
      summary: `Insert payload at index ${effectivePos}${effectivePos !== pos ? ` (requested ${pos}, clamped to document end)` : ""}.`,
      preview: base.slice(0, effectivePos) + payload + base.slice(effectivePos),
    };
  }

  if (command === "delete") {
    if (length === 0) {
      return {
        summary: "Invalid delete: backend rejects length 0 with INVALID_EDIT.",
        preview: base || "(empty document)",
      };
    }
    if (pos >= base.length) {
      return {
        summary: `Delete starts past the document end at index ${pos}, so the backend leaves the text unchanged.`,
        preview: base || "(empty document)",
      };
    }
    const actualLength = Math.min(length, base.length - pos);
    return {
      summary: `Delete ${actualLength} character(s) from index ${pos}${actualLength !== length ? ` (requested ${length}, clipped to remaining text)` : ""}.`,
      preview: base.slice(0, pos) + base.slice(pos + actualLength),
    };
  }

  if (command === "bold") {
    if (pos >= length) {
      return {
        summary: "Invalid bold range: backend requires start < end.",
        preview: base || "(empty document)",
      };
    }
    return {
      summary: `Wrap the range [${pos}, ${length}) with ** markers using the backend's insertion order.`,
      preview: applySortedInserts(base, [
        { pos: length, text: "**" },
        { pos: pos, text: "**" },
      ]),
    };
  }

  if (command === "italic") {
    if (pos >= length) {
      return {
        summary: "Invalid italic range: backend requires start < end.",
        preview: base || "(empty document)",
      };
    }
    return {
      summary: `Wrap the range [${pos}, ${length}) with * markers using the backend's insertion order.`,
      preview: applySortedInserts(base, [
        { pos: length, text: "*" },
        { pos: pos, text: "*" },
      ]),
    };
  }

  if (command === "heading") {
    if (length < 1 || length > 6) {
      return {
        summary: "Invalid heading level: backend accepts only levels 1 through 6.",
        preview: base || "(empty document)",
      };
    }
    const prefix = `${"#".repeat(length)} `;
    return {
      summary: `Insert heading prefix "${prefix}" at position ${Math.min(pos, base.length)}.`,
      preview: applySortedInserts(base, [{ pos, text: prefix }]),
    };
  }

  if (command === "newline") {
    return {
      summary: `Insert a newline at position ${Math.min(pos, base.length)}.`,
      preview: applySortedInserts(base, [{ pos, text: "\n" }]) || "(empty document)",
    };
  }

  return {
    summary: "No preview available.",
    preview: base || "(empty document)",
  };
}

function renderOperationPreview() {
  const result = computeEffectiveOperation();
  el("operation-summary").textContent = result.summary;
  el("operation-preview").textContent = result.preview || "(empty document)";
}

function syncControls() {
  const role = selectedUserRole();
  const roleBadge = el("selected-role");
  const roleHint = el("role-hint");
  const command = el("command");
  const commandHint = el("command-hint");
  const posHint = el("pos-hint");
  const lengthField = el("length-field");
  const lengthLabel = el("length-label");
  const lengthHint = el("length-hint");
  const payload = el("payload");
  const isWriter = role === "write";
  const meta = commandMeta[command.value] ?? commandMeta.get;

  if (!role) {
    roleBadge.textContent = "Unknown role";
    roleBadge.className = "badge";
    roleHint.innerHTML = "Connect the demo users first. By default, <code>daniel</code> and <code>yao</code> can write.";
    commandHint.innerHTML = "Read-only users should use <code>get</code>. Writer commands are enabled for users with the <code>write</code> role.";
    posHint.innerHTML = meta.posHint;
    lengthField.hidden = !meta.showLength;
    lengthHint.hidden = meta.showLength;
    lengthLabel.textContent = meta.lengthLabel ?? "Length / End / Level";
    lengthHint.innerHTML = meta.lengthHint;
    payload.placeholder = meta.payloadPlaceholder;
    payload.disabled = meta.disablePayload;
    renderOperationPreview();
    return;
  }

  roleBadge.textContent = isWriter ? "write user" : "read user";
  roleBadge.className = `badge ${isWriter ? "online" : ""}`.trim();
  roleHint.innerHTML = isWriter
    ? "This user can mutate the shared document. Every accepted write increments the version."
    : "This user is read-only by design. Use <code>get</code> or <code>Refresh Snapshot</code> to show collaborative visibility.";

  for (const option of command.options) {
    if (writeCommands.has(option.value)) {
      option.disabled = !isWriter;
    }
  }

  if (!isWriter && writeCommands.has(command.value)) {
    command.value = "get";
  }

  commandHint.innerHTML = isWriter
    ? meta.commandHint
    : "Edit commands are disabled here because this user only has read access in <code>roles.txt</code>.";
  posHint.innerHTML = meta.posHint;
  lengthField.hidden = !meta.showLength;
  lengthHint.hidden = meta.showLength;
  lengthLabel.textContent = meta.lengthLabel ?? "Length / End / Level";
  lengthHint.innerHTML = meta.lengthHint;
  payload.placeholder = meta.payloadPlaceholder;
  payload.disabled = meta.disablePayload;
  if (meta.disablePayload) {
    payload.value = "";
  }
  renderOperationPreview();
}

function renderSessions(sessions) {
  const root = el("sessions");
  root.innerHTML = "";

  if (!sessions.length) {
    root.innerHTML = `<article class="panel session-card"><p class="panel-label">Clients</p><h2>No active sessions yet</h2><p class="panel-copy">Connect the demo users to see their roles, cached versions, and live session PIDs.</p></article>`;
    return;
  }

  for (const session of sessions) {
    const card = document.createElement("article");
    card.className = "panel session-card";
    card.innerHTML = `
      <div class="session-meta">
        <div>
          <p class="panel-label">Client Session</p>
          <h2>${session.username}</h2>
        </div>
        <span class="badge ${session.connected ? "online" : "error"}">${session.role}</span>
      </div>
      <p class="panel-copy">PID ${session.pid} · cached version v${session.version} · ${session.length} chars</p>
      ${
        session.last_error
          ? `<p class="session-error">Last error: ${session.last_error}</p>`
          : `<p class="panel-copy">Ready for a refresh, write, or conflict demo.</p>`
      }
    `;
    root.appendChild(card);
  }
}

function renderTimeline(events) {
  const root = el("timeline");
  root.innerHTML = "";

  if (!events.length) {
    root.innerHTML = `<div class="timeline-item"><strong>No events yet.</strong><span class="panel-copy">Actions will appear here as the demo runs.</span></div>`;
    return;
  }

  for (const event of events) {
    const item = document.createElement("div");
    item.className = `timeline-item ${event.kind}`;
    item.innerHTML = `
      <div class="timeline-meta">
        <span>${event.timestamp}</span>
        <span>${event.kind}</span>
      </div>
      <strong>${event.message}</strong>
      <span class="panel-copy">${Object.entries(event)
        .filter(([key]) => !["timestamp", "kind", "message"].includes(key))
        .map(([key, value]) => `${key}: ${value}`)
        .join(" · ")}</span>
    `;
    root.appendChild(item);
  }
}

function renderDocument(snapshot) {
  el("doc-version").textContent = `v${snapshot.version}`;
  el("doc-length").textContent = `${snapshot.length} chars`;
  el("document-view").textContent = snapshot.document || "(empty document)";
}

function renderServer(server) {
  const badge = el("server-badge");
  badge.textContent = server.running ? "Online" : "Offline";
  badge.className = `badge ${server.running ? "online" : ""}`.trim();
  el("server-pid").textContent = server.pid ? `PID ${server.pid}` : "PID -";
  el("server-log").textContent = server.log.length ? server.log.join("\n") : "No backend activity yet.";
}

async function refreshState() {
  const response = await fetch(stateUrl);
  const data = await response.json();
  currentState = data;
  updateUserOptions();
  renderServer(data.server);
  renderDocument(data.snapshot);
  renderSessions(data.sessions);
  renderTimeline(data.events);
  syncControls();
  renderOperationPreview();
}

async function handleAction(action) {
  let result = null;

  if (action === "connect-demo") {
    result = await request("/api/connect-demo", { body: "{}" });
    setFeedback("Connected demo users. Daniel can write; Ryan and Yao can fetch the shared state.");
  } else if (action === "stale-demo") {
    result = await request("/api/demo/stale", { body: "{}" });
    setFeedback("Ran the stale write demo. One write should commit, increment the version, and a stale follow-up should be rejected.");
  } else if (action === "parallel-demo") {
    result = await request("/api/demo/parallel", { body: "{}" });
    setFeedback("Ran the parallel request demo with Daniel and Yao. Check the timeline for accepted and rejected requests.");
  } else if (action === "reset") {
    result = await request("/api/reset", { body: "{}" });
    setFeedback("Reset the demo. This also clears the live FIFO sessions.");
  } else if (action === "register-user") {
    const username = el("register-username").value.trim();
    const role = el("register-role").value;
    result = await request("/api/register", {
      body: JSON.stringify({ username, role }),
    });

    if (result.ok !== false) {
      await request("/api/connect", {
        body: JSON.stringify({ username }),
      });
      el("register-username").value = "";
      el("username").value = username;
      setFeedback(`Registered ${username} as ${role} and connected them to the backend.`);
    }
  } else if (action === "refresh") {
    result = await request("/api/refresh", {
      body: JSON.stringify({ username: el("username").value }),
    });
    setFeedback(`Fetched the latest snapshot for ${el("username").value}.`);
  } else if (action === "send-command") {
    result = await request("/api/command", {
      body: JSON.stringify({
        username: el("username").value,
        command: el("command").value,
        pos: Number(el("pos").value || 0),
        length: Number(el("length").value || 0),
        payload: el("payload").value,
      }),
    });

    if (result.ok === false) {
      setFeedback(`Request rejected: ${result.error}`, true);
    } else {
      setFeedback(`Sent ${el("command").value} as ${el("username").value}.`);
    }
  }

  if (result?.ok === false && action !== "send-command") {
    setFeedback(`Request rejected: ${result.error}`, true);
  }

  await refreshState();
}

document.addEventListener("click", (event) => {
  const button = event.target.closest("[data-action]");
  if (!button) {
    return;
  }
  handleAction(button.dataset.action).catch((error) => {
    console.error(error);
    setFeedback(`Action failed: ${error.message}`, true);
  });
});

el("username").addEventListener("change", () => {
  syncControls();
});

el("command").addEventListener("change", () => {
  const role = selectedUserRole();
  if (role !== "write" && writeCommands.has(el("command").value)) {
    el("command").value = "get";
  }
  syncControls();
});

["pos", "length", "payload"].forEach((id) => {
  el(id).addEventListener("input", () => {
    renderOperationPreview();
  });
});

refreshState().catch((error) => console.error(error));
syncProtocolWarning();
setInterval(() => {
  refreshState().catch(() => {});
}, 1500);
