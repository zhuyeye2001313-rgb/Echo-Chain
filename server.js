const http = require('http');
const fs   = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 3000;

// ── HTTP — serve the demo UI ──────────────────────────────────────────
const server = http.createServer((req, res) => {
  const filePath = path.join(__dirname, 'public', 'index.html');
  fs.readFile(filePath, (err, data) => {
    if (err) { res.writeHead(404); res.end('Not found'); return; }
    res.writeHead(200, { 'Content-Type': 'text/html' });
    res.end(data);
  });
});

// ── WebSocket relay ───────────────────────────────────────────────────
const wss = new WebSocketServer({ server });

// rooms[pair_id][device_id] = { ws, client_type }
// client_type: 'web' | 'device'
const rooms = {};

function getPartnerEntry(pair_id, device_id) {
  const room = rooms[pair_id];
  if (!room) return null;
  return Object.entries(room).find(([id]) => id !== device_id) || null;
}

function send(ws, payload) {
  if (ws && ws.readyState === 1) ws.send(JSON.stringify(payload));
}

wss.on('connection', (ws) => {
  let ctx = null; // { pair_id, device_id }

  ws.on('message', (raw) => {
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }

    // ── join ──────────────────────────────────────────────────────────
    if (msg.type === 'join') {
      const { pair_id, device_id } = msg;
      const client_type = msg.client_type || 'web';
      if (!pair_id || !device_id) return;

      rooms[pair_id] = rooms[pair_id] || {};

      // If slot already occupied, displace the old connection
      if (rooms[pair_id][device_id]) {
        send(rooms[pair_id][device_id].ws, { type: 'displaced', by: client_type });
      }

      rooms[pair_id][device_id] = { ws, client_type };
      ctx = { pair_id, device_id };

      // Confirm join to self
      send(ws, { type: 'joined', device_id, pair_id, client_type });

      // Notify both sides if partner already present
      const partnerEntry = getPartnerEntry(pair_id, device_id);
      if (partnerEntry) {
        const [partnerID, partnerSlot] = partnerEntry;
        // Tell partner a new client joined this slot
        send(partnerSlot.ws, { type: 'partner_joined', partner_id: device_id, partner_type: client_type });
        // Tell new client their partner is already here
        send(ws, { type: 'partner_joined', partner_id: partnerID, partner_type: partnerSlot.client_type });
      }

      console.log(`[${pair_id}] + ${device_id} (${client_type})`);
      return;
    }

    // ── note ──────────────────────────────────────────────────────────
    if (msg.type === 'note' && ctx) {
      const partnerEntry = getPartnerEntry(ctx.pair_id, ctx.device_id);
      if (partnerEntry) {
        const [, partnerSlot] = partnerEntry;
        send(partnerSlot.ws, { type: 'note', note_id: msg.note_id, sender_id: ctx.device_id });
      }
      const hasPartner = !!partnerEntry;
      console.log(`[${ctx.pair_id}] ${ctx.device_id} → note_${msg.note_id}  partner=${hasPartner ? 'online' : 'offline'}`);
    }
  });

  ws.on('close', () => {
    if (!ctx) return;
    const { pair_id, device_id } = ctx;
    const partnerEntry = getPartnerEntry(pair_id, device_id);
    if (partnerEntry) send(partnerEntry[1].ws, { type: 'partner_left' });
    if (rooms[pair_id]) {
      delete rooms[pair_id][device_id];
      if (!Object.keys(rooms[pair_id]).length) delete rooms[pair_id];
    }
    console.log(`[${pair_id}] - ${device_id}`);
  });
});

// Keepalive — ping every 25s to prevent Heroku H15 idle timeout (55s limit)
setInterval(() => {
  wss.clients.forEach((client) => {
    if (client.readyState === 1) client.ping();
  });
}, 25000);

server.listen(PORT, () => console.log(`Echo Chain relay → http://localhost:${PORT}`));
