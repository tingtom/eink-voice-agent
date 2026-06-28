(function () {
  "use strict";

  var SDK = window.__HERMES_PLUGIN_SDK__;
  if (!SDK) return;

  var React = SDK.React;
  var hooks = SDK.hooks;
  var useState = hooks.useState;
  var useEffect = hooks.useEffect;
  var useCallback = hooks.useCallback;
  var useRef = hooks.useRef;
  var comp = SDK.components;
  var Card = comp.Card;
  var CardHeader = comp.CardHeader;
  var CardTitle = comp.CardTitle;
  var CardContent = comp.CardContent;
  var Badge = comp.Badge;
  var Button = comp.Button;
  var Input = comp.Input;
  var Separator = comp.Separator;

  var h = React.createElement;
  var API = "/api/plugins/eink-voice-agent";

  // ── Helpers ────────────────────────────────────────────────────

  function timeAgo(iso) {
    if (!iso) return "";
    var diff = Date.now() - new Date(iso).getTime();
    var m = Math.floor(diff / 60000);
    if (m < 1) return "now";
    if (m < 60) return m + "m";
    var hh = Math.floor(m / 60);
    if (hh < 24) return hh + "h";
    return Math.floor(hh / 24) + "d";
  }

  function ts(iso) {
    if (!iso) return "";
    var d = new Date(iso);
    return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  }

  function fmtDur(sec) {
    if (sec == null) return "—";
    var hh = Math.floor(sec / 3600);
    var mm = Math.floor((sec % 3600) / 60);
    if (hh > 0) return hh + "h " + mm + "m";
    if (mm > 0) return mm + "m";
    return sec + "s";
  }

  function fmtStorage(kb) {
    if (kb == null) return "—";
    var mb = kb / 1024;
    if (mb >= 1024) return (mb / 1024).toFixed(1) + " GB";
    return mb.toFixed(1) + " MB";
  }

  function fmtDurShort(sec) {
    if (sec == null) return "";
    var hh = Math.floor(sec / 3600);
    var mm = Math.floor((sec % 3600) / 60);
    if (hh > 0) return hh + "h " + mm + "m";
    if (mm > 0) return mm + "m";
    return "&lt;1m";
  }

  // ── Status Header ──────────────────────────────────────────────

  function DashHeader(_ref) {
    var status = _ref.status;
    var todos = _ref.todos;
    var notes = _ref.notes;
    var transcript = _ref.transcript;
    var telemetry = _ref.telemetry;
    var onRefresh = _ref.onRefresh;
    var listening = status && status.listening;

    var tc = transcript ? transcript.length : 0;
    var hasTel = telemetry && telemetry.available && telemetry.data;

    return h(Card, { className: "eink-header" },
      h("div", { className: "eink-header-top" },
        h("div", { className: "eink-header-left" },
          h("div", { className: "eink-header-icon" },
            h("svg", { width: 20, height: 20, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2, strokeLinecap: "round", strokeLinejoin: "round" },
              h("rect", { x: 2, y: 2, width: 20, height: 14, rx: 2, ry: 2 }),
              h("line", { x1: 8, y1: 22, x2: 16, y2: 22 }),
              h("line", { x1: 12, y1: 16, x2: 12, y2: 22 })
            )
          ),
          h("div", {},
            h("div", { className: "eink-header-title" }, "E-Ink Voice Agent"),
            h("div", { className: "eink-header-sub" },
              "ws://0.0.0.0:", status ? String(status.port) : "8123"
            )
          )
        ),
        h("div", { className: "eink-header-right" },
          h(Badge, {
            className: listening ? "eink-badge-running" : "eink-badge-stopped"
          }, listening ? "\u25CF  Running" : "\u25CB  Stopped"),
          h(Button, { variant: "ghost", size: "sm", onClick: onRefresh, title: "Refresh" },
            h("svg", { width: 14, height: 14, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2, strokeLinecap: "round", strokeLinejoin: "round" },
              h("polyline", { points: "23 4 23 10 17 10" }),
              h("polyline", { points: "1 20 1 14 7 14" }),
              h("path", { d: "M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15" })
            )
          )
        )
      ),
      h("div", { className: "eink-header-stats" },
        h("div", { className: "eink-stat-chip" },
          h("span", { className: "eink-stat-num" }, todos ? String(todos.pending) : "—"),
          h("span", { className: "eink-stat-lbl" }, "pending todos")
        ),
        h("div", { className: "eink-stat-chip" },
          h("span", { className: "eink-stat-num" }, notes ? String(notes.length) : "—"),
          h("span", { className: "eink-stat-lbl" }, "notes")
        ),
        h("div", { className: "eink-stat-chip" },
          h("span", { className: "eink-stat-num" }, String(tc)),
          h("span", { className: "eink-stat-lbl" }, "messages")
        ),
        hasTel
          ? h("div", { className: "eink-stat-chip" },
              h("span", { className: "eink-stat-num" },
                fmtStorage(telemetry.data.storage_free_kb)
              ),
              h("span", { className: "eink-stat-lbl" }, "free")
            )
          : null,
        hasTel && telemetry.data.battery != null
          ? h("div", { className: "eink-stat-chip" },
              h("span", { className: "eink-stat-num eink-bat-" + (telemetry.data.battery < 20 ? "low" : telemetry.data.battery < 50 ? "mid" : "ok") },
                telemetry.data.battery + "%"
              ),
              h("span", { className: "eink-stat-lbl" }, "battery")
            )
          : null
      )
    );
  }

  // ── Telemetry Panel ────────────────────────────────────────────

  function TelemetryPanel(_ref2) {
    var data = _ref2.data;
    if (!data) {
      return h("div", { className: "eink-empty" },
        "Waiting for device telemetry..."
      );
    }

    var bat = data.battery;
    var wifi = data.wifi_rssi;
    var storage = data.storage_free_kb;
    var recSec = data.recording_time_remaining_sec;

    var batColor = "#22c55e";
    if (bat != null) {
      if (bat < 20) batColor = "#ef4444";
      else if (bat < 50) batColor = "#eab308";
    }

    var wifiLabel = "—";
    var wifiBars = 0;
    if (wifi != null) {
      if (wifi > -50) { wifiLabel = "Excellent"; wifiBars = 4; }
      else if (wifi > -65) { wifiLabel = "Good"; wifiBars = 3; }
      else if (wifi > -80) { wifiLabel = "Fair"; wifiBars = 2; }
      else { wifiLabel = "Weak"; wifiBars = 1; }
    }

    return h(Card, { className: "eink-tele-card" },
      h(CardContent, { className: "eink-tele-grid" },
        // Battery
        h("div", { className: "eink-tele-metric" },
          h("div", { className: "eink-tele-metric-hd" },
            h("span", { className: "eink-tele-icon" },
              h("svg", { width: 14, height: 14, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2 },
                h("rect", { x: 1, y: 6, width: 18, height: 12, rx: 2 }),
                h("line", { x1: 23, y1: 10, x2: 23, y2: 14 })
              )
            ),
            h("span", {}, "Battery")
          ),
          h("div", { className: "eink-tele-val", style: { color: batColor } },
            bat != null ? bat + "%" : "—"
          ),
          bat != null
            ? h("div", { className: "eink-bar-bg" },
                h("div", { className: "eink-bar-fill", style: { width: bat + "%", backgroundColor: batColor } })
              )
            : null
        ),
        // WiFi
        h("div", { className: "eink-tele-metric" },
          h("div", { className: "eink-tele-metric-hd" },
            h("span", { className: "eink-tele-icon" },
              h("svg", { width: 14, height: 14, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2 },
                h("path", { d: "M5 12.55a11 11 0 0 1 14.08 0" }),
                h("path", { d: "M1.42 9a16 16 0 0 1 21.16 0" }),
                h("path", { d: "M8.53 16.11a6 6 0 0 1 6.95 0" }),
                h("circle", { cx: 12, cy: 20, r: 1 })
              )
            ),
            h("span", {}, "WiFi")
          ),
          h("div", { className: "eink-tele-val" },
            wifi != null ? wifi + " dBm" : "—"
          ),
          wifi != null
            ? h("div", { className: "eink-wifi-indicator" },
                h("div", { className: "eink-wifi-dots" },
                  Array(4).fill(0).map(function (_, i) {
                    return h("div", {
                      key: i,
                      className: "eink-wifi-dot" + (i < wifiBars ? " eink-wifi-on" : "")
                    });
                  })
                ),
                h("span", { className: "eink-wifi-label" }, wifiLabel)
              )
            : null
        ),
        // Uptime
        h("div", { className: "eink-tele-metric" },
          h("div", { className: "eink-tele-metric-hd" },
            h("span", { className: "eink-tele-icon" },
              h("svg", { width: 14, height: 14, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2 },
                h("circle", { cx: 12, cy: 12, r: 10 }),
                h("polyline", { points: "12 6 12 12 16 14" })
              )
            ),
            h("span", {}, "Uptime")
          ),
          h("div", { className: "eink-tele-val" }, fmtDur(data.uptime)),
          h("div", { className: "eink-tele-sub" }, "since last boot")
        ),
        // Wake count
        h("div", { className: "eink-tele-metric" },
          h("div", { className: "eink-tele-metric-hd" },
            h("span", { className: "eink-tele-icon" },
              h("svg", { width: 14, height: 14, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2 },
                h("path", { d: "M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9" }),
                h("path", { d: "M13.73 21a2 2 0 0 1-3.46 0" })
              )
            ),
            h("span", {}, "Wake count")
          ),
          h("div", { className: "eink-tele-val" },
            data.wake_count != null ? String(data.wake_count) : "—"
          ),
          h("div", { className: "eink-tele-sub" }, "deep-sleep wake cycles")
        ),
        // Storage
        h("div", { className: "eink-tele-metric" },
          h("div", { className: "eink-tele-metric-hd" },
            h("span", { className: "eink-tele-icon" },
              h("svg", { width: 14, height: 14, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2 },
                h("ellipse", { cx: 12, cy: 5, rx: 9, ry: 3 }),
                h("path", { d: "M21 12c0 1.66-4 3-9 3s-9-1.34-9-3" }),
                h("path", { d: "M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5" })
              )
            ),
            h("span", {}, "Storage")
          ),
          h("div", { className: "eink-tele-val" }, fmtStorage(storage)),
          recSec != null
            ? h("div", { className: "eink-tele-sub" },
                "\u2248 " + fmtDurShort(recSec) + " of recording"
              )
            : h("div", { className: "eink-tele-sub" }, "free space")
        ),
        // Last seen
        h("div", { className: "eink-tele-metric" },
          h("div", { className: "eink-tele-metric-hd" },
            h("span", { className: "eink-tele-icon" },
              h("svg", { width: 14, height: 14, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 2 },
                h("circle", { cx: 12, cy: 12, r: 10 }),
                h("polyline", { points: "12 6 12 12 16 14" })
              )
            ),
            h("span", {}, "Last seen")
          ),
          h("div", { className: "eink-tele-val" }, data.last_seen ? timeAgo(data.last_seen) : "—"),
          h("div", { className: "eink-tele-sub" }, data.last_seen ? ts(data.last_seen) : "")
        )
      )
    );
  }

  // ── Transcript ─────────────────────────────────────────────────

  function TranscriptFeed(_ref3) {
    var transcript = _ref3.transcript;
    var scrollRef = useRef(null);

    useEffect(function () {
      if (scrollRef.current) {
        scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
      }
    }, [transcript]);

    if (!transcript || transcript.length === 0) {
      return h("div", { className: "eink-empty" },
        "No conversations yet. Speak to the device to see messages here."
      );
    }

    return h("div", { className: "eink-chat", ref: scrollRef },
      transcript.map(function (m, i) {
        var isIn = m.direction === "in";
        return h("div", {
          key: i,
          className: isIn ? "eink-msg eink-msg-in" : "eink-msg eink-msg-out"
        },
          h("div", { className: "eink-msg-sender" },
            isIn ? "Device" : "Assistant"
          ),
          h("div", { className: "eink-msg-bubble" }, m.content),
          h("div", { className: "eink-msg-time" }, ts(m.ts))
        );
      })
    );
  }

  // ── Todos ───────────────────────────────────────────────────────

  function TodosPanel(_ref4) {
    var todos = _ref4.todos;
    var onRefresh = _ref4.onRefresh;
    var _useState = useState("");
    var text = _useState[0];
    var setText = _useState[1];
    var _useState2 = useState(false);
    var adding = _useState2[0];
    var setAdding = _useState2[1];

    var add = useCallback(function () {
      var t = text.trim();
      if (!t) return;
      setAdding(true);
      SDK.fetchJSON(API + "/todos", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ text: t })
      }).then(function () {
        setText(""); setAdding(false); onRefresh();
      }).catch(function () { setAdding(false); });
    }, [text, onRefresh]);

    var toggle = useCallback(function (id) {
      SDK.fetchJSON(API + "/todos/" + id + "/toggle", { method: "POST" })
        .then(function () { onRefresh(); });
    }, [onRefresh]);

    var del = useCallback(function (id) {
      SDK.fetchJSON(API + "/todos/" + id, { method: "DELETE" })
        .then(function () { onRefresh(); });
    }, [onRefresh]);

    var items = todos && todos.items ? todos.items : [];
    var done = todos ? todos.done : 0;
    var total = todos ? todos.total : 0;

    return h("div", {},
      h("div", { className: "eink-todo-form" },
        h(Input, {
          value: text,
          onChange: function (e) { return setText(e.target.value); },
          onKeyDown: function (e) { if (e.key === "Enter") add(); },
          placeholder: "Add a todo…"
        }),
        h(Button, { onClick: add, disabled: adding || !text.trim(), size: "sm" }, "Add")
      ),
      total > 0
        ? h("div", { className: "eink-todo-progress" },
            h("div", { className: "eink-todo-bar-bg" },
              h("div", {
                className: "eink-todo-bar-fill",
                style: { width: (done / total * 100) + "%" }
              })
            ),
            h("span", { className: "eink-todo-progress-label" },
              done + " of " + total + " done"
            )
          )
        : null,
      items.length > 0
        ? h("div", { className: "eink-todo-list" },
            items.map(function (t) {
              return h("div", {
                key: t.id,
                className: "eink-todo-row" + (t.done ? " eink-todo-done" : "")
              },
                h("div", {
                  className: "eink-todo-check" + (t.done ? " eink-checked" : ""),
                  onClick: function () { toggle(t.id); }
                },
                  t.done ? h("svg", { width: 12, height: 12, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 3 },
                    h("polyline", { points: "20 6 9 17 4 12" })
                  ) : null
                ),
                h("span", { className: "eink-todo-text" }, t.text),
                h("button", {
                  className: "eink-todo-del",
                  onClick: function () { del(t.id); },
                  title: "Delete"
                }, "\u00D7")
              );
            })
          )
        : h("div", { className: "eink-empty" }, "No todos yet. Add one above!")
    );
  }

  // ── Notes ────────────────────────────────────────────────────────

  function NotesPanel(_ref5) {
    var notes = _ref5.notes;
    var onRefresh = _ref5.onRefresh;
    var _useState3 = useState(null);
    var content = _useState3[0];
    var setContent = _useState3[1];

    var open = useCallback(function (name) {
      SDK.fetchJSON(API + "/notes/" + encodeURIComponent(name))
        .then(function (n) { setContent(n); });
    }, []);

    var del = useCallback(function (name) {
      SDK.fetchJSON(API + "/notes/" + encodeURIComponent(name), { method: "DELETE" })
        .then(function () {
          if (content && content.name === name) setContent(null);
          onRefresh();
        });
    }, [onRefresh, content]);

    var list = notes && notes.length > 0 ? notes : [];

    return h("div", {},
      list.length > 0
        ? h("div", { className: "eink-notes-list" },
            list.slice(0, 10).map(function (n) {
              var active = content && content.name === n.name;
              return h("div", {
                key: n.name,
                className: "eink-note-row" + (active ? " active" : ""),
                onClick: function () { open(n.name); }
              },
                h("div", { className: "eink-note-top" },
                  h("span", { className: "eink-note-name" }, n.name),
                  h("span", { className: "eink-note-time" }, timeAgo(n.modified))
                ),
                h("div", { className: "eink-note-preview" }, n.preview || "(empty)")
              );
            })
          )
        : h("div", { className: "eink-empty" }, "No notes yet."),

      content
        ? h("div", { className: "eink-note-view" },
            h("div", { className: "eink-note-view-hd" },
              h("strong", {}, content.name),
              h("div", { className: "eink-note-view-actions" },
                h(Button, { variant: "destructive", size: "sm",
                  onClick: function () { del(content.name); }
                }, "Delete"),
                h(Button, { variant: "ghost", size: "sm",
                  onClick: function () { setContent(null); }
                }, "Close")
              )
            ),
            h("pre", { className: "eink-note-body" }, content.content || "(empty)")
          )
        : null
    );
  }

  // ── Activity ────────────────────────────────────────────────────

  function ActivityFeed(_ref6) {
    var activity = _ref6.activity;
    if (!activity || activity.length === 0) {
      return h("div", { className: "eink-empty" }, "No events yet.");
    }
    return h("div", { className: "eink-events" },
      activity.slice(0, 30).map(function (e, i) {
        return h("div", { key: i, className: "eink-event" },
          h("span", { className: "eink-event-type" }, e.type),
          h("span", { className: "eink-event-time" }, ts(e.ts)),
          e.detail ? h("span", { className: "eink-event-detail" }, e.detail) : null
        );
      })
    );
  }

  // ── Subsection wrapper (collapsible) ─────────────────────────────

  function Subsection(_ref7) {
    var title = _ref7.title;
    var children = _ref7.children;
    var open_ = _ref7.defaultOpen === undefined ? true : _ref7.defaultOpen;
    var badge = _ref7.badge;
    var _useState4 = useState(open_);
    var isOpen = _useState4[0];
    var setIsOpen = _useState4[1];
    return h("div", { className: "eink-sub" },
      h("div", { className: "eink-sub-hd", onClick: function () { setIsOpen(!isOpen); } },
        h("span", { className: "eink-sub-chev" }, isOpen ? "\u25BE" : "\u25B8"),
        h("span", { className: "eink-sub-title" }, title),
        badge != null
          ? h(Badge, { variant: "secondary", className: "eink-sub-badge" }, badge)
          : null
      ),
      isOpen ? h("div", { className: "eink-sub-body" }, children) : null
    );
  }

  // ── Main Dashboard ──────────────────────────────────────────────

  function EInkDashboard() {
    var _s = useState(true);
    var loading = _s[0];
    var setLoading = _s[1];
    var _s2 = useState(null);
    var status = _s2[0];
    var setStatus = _s2[1];
    var _s3 = useState(null);
    var todos = _s3[0];
    var setTodos = _s3[1];
    var _s4 = useState(null);
    var notes = _s4[0];
    var setNotes = _s4[1];
    var _s5 = useState(null);
    var activity = _s5[0];
    var setActivity = _s5[1];
    var _s6 = useState(null);
    var transcript = _s6[0];
    var setTranscript = _s6[1];
    var _s7 = useState(null);
    var telemetry = _s7[0];
    var setTelemetry = _s7[1];
    var pollRef = useRef(null);

    var fetchAll = useCallback(function () {
      setLoading(true);
      Promise.all([
        SDK.fetchJSON(API + "/status"),
        SDK.fetchJSON(API + "/todos"),
        SDK.fetchJSON(API + "/notes"),
        SDK.fetchJSON(API + "/activity"),
        SDK.fetchJSON(API + "/transcript"),
        SDK.fetchJSON(API + "/telemetry"),
      ]).then(function (r) {
        setStatus(r[0]); setTodos(r[1]); setNotes(r[2]);
        setActivity(r[3]); setTranscript(r[4]); setTelemetry(r[5]);
      }).catch(function (e) {
        console.error("eink fetch error", e);
      }).finally(function () { setLoading(false); });
    }, []);

    useEffect(function () {
      fetchAll();
      pollRef.current = setInterval(fetchAll, 10000);
      return function () { clearInterval(pollRef.current); };
    }, [fetchAll]);

    if (loading && !status) {
      return h("div", { className: "eink-loading" }, "Loading E-Ink dashboard...");
    }

    return h("div", { className: "eink-root" },
      h(DashHeader, {
        status: status, todos: todos, notes: notes,
        transcript: transcript, telemetry: telemetry,
        onRefresh: fetchAll
      }),

      h(TelemetryPanel, {
        data: telemetry && telemetry.available ? telemetry.data : null
      }),

      h(Subsection, { title: "Transcript", badge: transcript ? String(transcript.length) : "0" },
        h(TranscriptFeed, { transcript: transcript })
      ),

      h(Subsection, { title: "Todos", badge: todos ? todos.pending + " pending" : null },
        h(TodosPanel, { todos: todos, onRefresh: fetchAll })
      ),

      h(Subsection, { title: "Notes", badge: notes ? String(notes.length) : null },
        h(NotesPanel, { notes: notes, onRefresh: fetchAll })
      ),

      h(Subsection, { title: "Activity", defaultOpen: false,
          badge: activity ? String(activity.length) : null },
        h(ActivityFeed, { activity: activity })
      )
    );
  }

  // ── Register ────────────────────────────────────────────────────
  if (window.__HERMES_PLUGINS__ && window.__HERMES_PLUGINS__.register) {
    window.__HERMES_PLUGINS__.register("eink-voice-agent", EInkDashboard);
  }
})();
