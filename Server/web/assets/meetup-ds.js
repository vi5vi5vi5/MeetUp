/* @ds-bundle: {"format":4,"namespace":"MeetUpDesignSystem_2584b5","components":[{"name":"ChatMessage","sourcePath":"components/conference/ChatMessage.jsx"},{"name":"ControlDock","sourcePath":"components/conference/ControlDock.jsx"},{"name":"ParticipantRow","sourcePath":"components/conference/ParticipantRow.jsx"},{"name":"VideoTile","sourcePath":"components/conference/VideoTile.jsx"},{"name":"Badge","sourcePath":"components/core/Badge.jsx"},{"name":"Button","sourcePath":"components/core/Button.jsx"},{"name":"Card","sourcePath":"components/core/Card.jsx"},{"name":"Icon","sourcePath":"components/core/Icon.jsx"},{"name":"IconButton","sourcePath":"components/core/IconButton.jsx"},{"name":"Input","sourcePath":"components/forms/Input.jsx"},{"name":"Field","sourcePath":"components/forms/Input.jsx"}],"sourceHashes":{"components/conference/ChatMessage.jsx":"ff8e8559608e","components/conference/ControlDock.jsx":"8e6d79062f70","components/conference/ParticipantRow.jsx":"98f014100270","components/conference/VideoTile.jsx":"4b98752832d3","components/core/Badge.jsx":"530302720391","components/core/Button.jsx":"86aeb4f7d82b","components/core/Card.jsx":"d8149001ff3c","components/core/Icon.jsx":"b562be0fd8a8","components/core/IconButton.jsx":"986e8fcc5021","components/forms/Input.jsx":"71ad91c44412","ui_kits/web/ConferenceScreen.jsx":"979eae5c9cea","ui_kits/web/LobbyScreen.jsx":"100132a263a5"},"inlinedExternals":[],"unexposedExports":[{"name":"iconNames","sourcePath":"components/core/Icon.jsx"}]} */

(() => {

const __ds_ns = (window.MeetUpDesignSystem_2584b5 = window.MeetUpDesignSystem_2584b5 || {});

const __ds_scope = {};

(__ds_ns.__errors = __ds_ns.__errors || []);

// components/conference/ChatMessage.jsx
try { (() => {
/**
 * ChatMessage — a chat entry. `self` right-aligns and fills the bubble with
 * accent; others are inset surface bubbles on the left.
 */
function ChatMessage({
  author,
  time,
  text,
  self = false,
  style
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: "flex",
      flexDirection: "column",
      alignItems: self ? "flex-end" : "flex-start",
      fontFamily: "var(--font-ui)",
      ...style
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      fontSize: "var(--text-2xs)",
      color: "var(--text-faint)",
      margin: "0 4px 3px",
      display: "flex",
      gap: "6px"
    }
  }, /*#__PURE__*/React.createElement("b", {
    style: {
      fontWeight: "var(--weight-bold)",
      color: self ? "var(--accent-ink)" : "var(--text-muted)"
    }
  }, self ? "Вы" : author), /*#__PURE__*/React.createElement("span", null, time)), /*#__PURE__*/React.createElement("div", {
    style: {
      background: self ? "var(--accent)" : "var(--surface-2)",
      color: self ? "var(--on-accent)" : "var(--text)",
      padding: "9px 12px",
      borderRadius: "var(--radius-sm)",
      borderTopRightRadius: self ? "3px" : "var(--radius-sm)",
      borderTopLeftRadius: self ? "var(--radius-sm)" : "3px",
      maxWidth: "82%",
      fontSize: "var(--text-sm)",
      fontWeight: "var(--weight-medium)",
      lineHeight: "var(--leading-snug)",
      wordWrap: "break-word"
    }
  }, text));
}
Object.assign(__ds_scope, { ChatMessage });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/conference/ChatMessage.jsx", error: String((e && e.message) || e) }); }

// components/core/Badge.jsx
try { (() => {
/**
 * Badge — small status pill. tone drives colour; `dot` prepends a status dot;
 * `solid` gives a filled lime/red chip.
 */
function Badge({
  children,
  tone = "muted",
  dot = false,
  solid = false,
  style
}) {
  const inkByTone = {
    muted: "var(--text-muted)",
    live: "var(--live)",
    accent: "var(--accent-ink)",
    danger: "var(--danger)"
  };
  const ink = inkByTone[tone] || inkByTone.muted;
  const solidBg = tone === "danger" ? "var(--danger)" : "var(--accent)";
  const solidInk = tone === "danger" ? "var(--on-danger)" : "var(--on-accent)";
  return /*#__PURE__*/React.createElement("span", {
    style: {
      display: "inline-flex",
      alignItems: "center",
      gap: "7px",
      fontSize: "var(--text-xs)",
      fontWeight: "var(--weight-semibold)",
      fontFamily: "var(--font-ui)",
      padding: "6px 11px",
      borderRadius: "var(--radius-pill)",
      color: solid ? solidInk : ink,
      background: solid ? solidBg : "var(--surface)",
      border: solid ? "1px solid transparent" : "1px solid var(--border)",
      ...style
    }
  }, dot && /*#__PURE__*/React.createElement("span", {
    style: {
      width: "8px",
      height: "8px",
      borderRadius: "var(--radius-full)",
      background: solid ? solidInk : ink,
      flex: "none"
    }
  }), children);
}
Object.assign(__ds_scope, { Badge });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Badge.jsx", error: String((e && e.message) || e) }); }

// components/core/Card.jsx
try { (() => {
/**
 * Card — surface container. Prism Minimal: hairline border, 16px radius,
 * soft theme-aware shadow when `elevated`.
 */
function Card({
  children,
  title,
  subtitle,
  elevated = false,
  padding,
  width,
  style
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      background: "var(--surface)",
      border: "1px solid var(--border)",
      borderRadius: "var(--radius-card)",
      padding: padding != null ? padding : "var(--pad-card)",
      width: typeof width === "number" ? `${width}px` : width,
      boxShadow: elevated ? "var(--shadow-card)" : "none",
      color: "var(--text)",
      fontFamily: "var(--font-ui)",
      ...style
    }
  }, title && /*#__PURE__*/React.createElement("h2", {
    style: {
      margin: "0 0 4px",
      fontFamily: "var(--font-display)",
      fontSize: "var(--text-xl)",
      fontWeight: "var(--weight-bold)",
      letterSpacing: "var(--tracking-display)"
    }
  }, title), subtitle && /*#__PURE__*/React.createElement("p", {
    style: {
      margin: "0 0 18px",
      color: "var(--text-muted)",
      fontSize: "var(--text-sm)"
    }
  }, subtitle), children);
}
Object.assign(__ds_scope, { Card });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Card.jsx", error: String((e && e.message) || e) }); }

// components/core/Icon.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * Icon — thin 2px stroked glyphs matching MeetUp's Prism Minimal aesthetic.
 * Path data is from Lucide (ISC-licensed, https://lucide.dev) — a substitution,
 * since the source client shipped no icon set (see readme.md ICONOGRAPHY).
 *
 * Stored as plain [tag, attrs] tuples (NOT React elements) so nothing touches
 * React at module-load time — elements are built inside render.
 */
const PATHS = {
  mic: [["path", {
    d: "M12 2a3 3 0 0 0-3 3v6a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3z"
  }], ["path", {
    d: "M19 10v1a7 7 0 0 1-14 0v-1"
  }], ["line", {
    x1: 12,
    y1: 19,
    x2: 12,
    y2: 23
  }]],
  "mic-off": [["line", {
    x1: 2,
    y1: 2,
    x2: 22,
    y2: 22
  }], ["path", {
    d: "M18.89 13.23A7.12 7.12 0 0 0 19 12v-1"
  }], ["path", {
    d: "M5 11v1a7 7 0 0 0 10.71 5.95"
  }], ["path", {
    d: "M9 9v3a3 3 0 0 0 5.12 2.12M15 9.34V5a3 3 0 0 0-5.94-.6"
  }], ["line", {
    x1: 12,
    y1: 19,
    x2: 12,
    y2: 23
  }]],
  video: [["path", {
    d: "M23 7l-7 5 7 5V7z"
  }], ["rect", {
    x: 1,
    y: 5,
    width: 15,
    height: 14,
    rx: 2
  }]],
  "video-off": [["path", {
    d: "M16 16v2a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h2m4 0h4a2 2 0 0 1 2 2v4l4-4v10"
  }], ["line", {
    x1: 2,
    y1: 2,
    x2: 22,
    y2: 22
  }]],
  screen: [["rect", {
    x: 2,
    y: 3,
    width: 20,
    height: 14,
    rx: 2
  }], ["path", {
    d: "M8 21h8M12 17v4"
  }]],
  "phone-off": [["path", {
    d: "M10.68 13.31a16 16 0 0 0 3.41 2.6l1.27-1.27a2 2 0 0 1 2.11-.45 12.84 12.84 0 0 0 2.29.62 2 2 0 0 1 1.72 2v2.92a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.42 19.42 0 0 1-3.33-2.67m-2.67-3.34a19.79 19.79 0 0 1-3.07-8.66A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72 12.84 12.84 0 0 0 .7 2.81 2 2 0 0 1-.45 2.11L8.09 9.91",
    transform: "rotate(135 12 12)"
  }]],
  chat: [["path", {
    d: "M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"
  }]],
  users: [["path", {
    d: "M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"
  }], ["circle", {
    cx: 9,
    cy: 7,
    r: 4
  }], ["path", {
    d: "M23 21v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75"
  }]],
  settings: [["circle", {
    cx: 12,
    cy: 12,
    r: 3
  }], ["path", {
    d: "M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"
  }]],
  "arrow-right": [["line", {
    x1: 5,
    y1: 12,
    x2: 19,
    y2: 12
  }], ["polyline", {
    points: "12 5 19 12 12 19"
  }]],
  send: [["line", {
    x1: 22,
    y1: 2,
    x2: 11,
    y2: 13
  }], ["polygon", {
    points: "22 2 15 22 11 13 2 9 22 2"
  }]],
  x: [["line", {
    x1: 18,
    y1: 6,
    x2: 6,
    y2: 18
  }], ["line", {
    x1: 6,
    y1: 6,
    x2: 18,
    y2: 18
  }]],
  check: [["polyline", {
    points: "20 6 9 17 4 12"
  }]],
  plus: [["line", {
    x1: 12,
    y1: 5,
    x2: 12,
    y2: 19
  }], ["line", {
    x1: 5,
    y1: 12,
    x2: 19,
    y2: 12
  }]],
  hand: [["path", {
    d: "M18 11V6a2 2 0 0 0-2-2 2 2 0 0 0-2 2M14 10V4a2 2 0 0 0-2-2 2 2 0 0 0-2 2v2M10 10.5V6a2 2 0 0 0-2-2 2 2 0 0 0-2 2v8M18 8a2 2 0 1 1 4 0v6a8 8 0 0 1-8 8h-2c-2.8 0-4.5-.86-5.99-2.34l-3.6-3.6a2 2 0 0 1 2.83-2.82L7 15"
  }]],
  more: [["circle", {
    cx: 12,
    cy: 12,
    r: 1
  }], ["circle", {
    cx: 19,
    cy: 12,
    r: 1
  }], ["circle", {
    cx: 5,
    cy: 12,
    r: 1
  }]],
  sun: [["circle", {
    cx: 12,
    cy: 12,
    r: 4
  }], ["path", {
    d: "M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"
  }]],
  moon: [["path", {
    d: "M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"
  }]],
  copy: [["rect", {
    x: 9,
    y: 9,
    width: 13,
    height: 13,
    rx: 2
  }], ["path", {
    d: "M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"
  }]]
};
function Icon({
  name,
  size = 20,
  strokeWidth = 2,
  style,
  ...rest
}) {
  const parts = PATHS[name] || [];
  return /*#__PURE__*/React.createElement("svg", _extends({
    viewBox: "0 0 24 24",
    width: size,
    height: size,
    fill: "none",
    stroke: "currentColor",
    strokeWidth: strokeWidth,
    strokeLinecap: "round",
    strokeLinejoin: "round",
    style: {
      display: "block",
      flex: "none",
      ...style
    },
    "aria-hidden": "true"
  }, rest), parts.map(([tag, attrs], i) => React.createElement(tag, {
    key: i,
    ...attrs
  })));
}
function iconNames() {
  return Object.keys(PATHS);
}
Object.assign(__ds_scope, { Icon, iconNames });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Icon.jsx", error: String((e && e.message) || e) }); }

// components/conference/ParticipantRow.jsx
try { (() => {
/**
 * ParticipantRow — one entry in the side-panel list.
 * Initials chip · name · optional "вы" tag · mic status.
 */
function ParticipantRow({
  name,
  initials,
  you = false,
  muted = false,
  host = false,
  style
}) {
  const [hover, setHover] = React.useState(false);
  const ini = initials || (name ? name.trim().slice(0, 2).toUpperCase() : "");
  return /*#__PURE__*/React.createElement("div", {
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => setHover(false),
    style: {
      display: "flex",
      alignItems: "center",
      gap: "12px",
      padding: "8px 10px",
      borderRadius: "var(--radius-sm)",
      background: hover ? "var(--surface-2)" : "transparent",
      transition: "var(--transition)",
      fontFamily: "var(--font-ui)",
      ...style
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      width: "32px",
      height: "32px",
      borderRadius: "var(--radius-full)",
      background: you ? "linear-gradient(160deg, var(--tile-self-from), var(--tile-self-to))" : "linear-gradient(160deg, var(--tile-from), var(--tile-to))",
      border: "1px solid var(--border)",
      display: "flex",
      alignItems: "center",
      justifyContent: "center",
      fontSize: "var(--text-2xs)",
      fontWeight: "var(--weight-bold)",
      color: you ? "var(--accent-ink)" : "var(--text)",
      flex: "none"
    }
  }, ini), /*#__PURE__*/React.createElement("span", {
    style: {
      display: "flex",
      flexDirection: "column",
      minWidth: 0,
      flex: 1
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      fontSize: "var(--text-sm)",
      fontWeight: "var(--weight-semibold)",
      color: "var(--text)"
    }
  }, name, you ? " (вы)" : ""), host && /*#__PURE__*/React.createElement("span", {
    style: {
      fontSize: "var(--text-2xs)",
      color: "var(--accent-ink)",
      fontWeight: "var(--weight-semibold)"
    }
  }, "\u043E\u0440\u0433\u0430\u043D\u0438\u0437\u0430\u0442\u043E\u0440")), /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: muted ? "mic-off" : "mic",
    size: 16,
    style: {
      color: muted ? "var(--text-faint)" : "var(--text-muted)",
      flex: "none"
    }
  }));
}
Object.assign(__ds_scope, { ParticipantRow });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/conference/ParticipantRow.jsx", error: String((e && e.message) || e) }); }

// components/conference/VideoTile.jsx
try { (() => {
/**
 * VideoTile — a participant cell. MONOCHROME tile ramp (dark greys in dark
 * theme, light greys in light theme — the only chroma is the accent used on
 * your own tile). Shows initials placeholder, frosted name chip, and an
 * optional muted-mic indicator.
 */
function VideoTile({
  name,
  initials,
  self = false,
  speaking = false,
  muted = false,
  src,
  aspect = "16 / 11",
  style,
  children
}) {
  const ini = initials || (name ? name.trim().slice(0, 2).toUpperCase() : "");
  return /*#__PURE__*/React.createElement("div", {
    style: {
      position: "relative",
      aspectRatio: aspect,
      borderRadius: "var(--radius-lg)",
      overflow: "hidden",
      background: self ? "linear-gradient(160deg, var(--tile-self-from), var(--tile-self-to))" : "linear-gradient(160deg, var(--tile-from), var(--tile-to))",
      border: speaking ? "2px solid var(--accent)" : "1px solid var(--border)",
      fontFamily: "var(--font-ui)",
      ...style
    }
  }, src ? /*#__PURE__*/React.createElement("img", {
    src: src,
    alt: name,
    style: {
      width: "100%",
      height: "100%",
      objectFit: "cover",
      display: "block"
    }
  }) : children ? children : /*#__PURE__*/React.createElement("div", {
    style: {
      position: "absolute",
      inset: 0,
      display: "flex",
      alignItems: "center",
      justifyContent: "center",
      fontFamily: "var(--font-display)",
      fontWeight: "var(--weight-bold)",
      fontSize: "clamp(22px, 6cqw, 40px)",
      letterSpacing: "var(--tracking-display)",
      color: self ? "var(--accent-ink)" : "var(--text)",
      opacity: self ? 1 : 0.85,
      containerType: "inline-size"
    }
  }, ini), /*#__PURE__*/React.createElement("div", {
    style: {
      position: "absolute",
      left: "10px",
      bottom: "10px",
      display: "flex",
      alignItems: "center",
      gap: "6px",
      background: "var(--scrim-chip)",
      backdropFilter: "var(--blur-chip)",
      WebkitBackdropFilter: "var(--blur-chip)",
      padding: "5px 10px",
      borderRadius: "var(--radius-xs)",
      fontSize: "var(--text-xs)",
      fontWeight: "var(--weight-semibold)",
      color: "var(--text)"
    }
  }, muted && /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: "mic-off",
    size: 13,
    style: {
      color: "var(--danger)"
    }
  }), /*#__PURE__*/React.createElement("span", null, name, self ? " (вы)" : "")));
}
Object.assign(__ds_scope, { VideoTile });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/conference/VideoTile.jsx", error: String((e && e.message) || e) }); }

// components/core/Button.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * MeetUp Button — Prism Minimal.
 * primary  = lime fill, dark text
 * secondary = surface fill + hairline border
 * ghost    = transparent, border, fills on hover
 * danger   = red fill
 */
function Button({
  children,
  variant = "primary",
  size = "md",
  icon,
  iconRight,
  disabled = false,
  block = false,
  type = "button",
  onClick,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const sizes = {
    sm: {
      padding: "8px 12px",
      fontSize: "var(--text-xs)",
      gap: "6px"
    },
    md: {
      padding: "11px 18px",
      fontSize: "var(--text-md)",
      gap: "8px"
    },
    lg: {
      padding: "14px 22px",
      fontSize: "var(--text-lg)",
      gap: "10px"
    }
  };
  const variants = {
    primary: {
      color: "var(--on-accent)",
      background: "var(--accent)",
      border: "1px solid transparent",
      filter: hover ? "brightness(1.06)" : "none"
    },
    secondary: {
      color: "var(--text)",
      background: hover ? "var(--surface-2)" : "var(--surface)",
      border: "1px solid var(--border)"
    },
    ghost: {
      color: "var(--text)",
      background: hover ? "var(--surface-2)" : "transparent",
      border: "1px solid var(--border)"
    },
    danger: {
      color: "var(--on-danger)",
      background: "var(--danger)",
      border: "1px solid transparent",
      filter: hover ? "brightness(1.06)" : "none"
    }
  };
  const iconSize = size === "lg" ? 20 : size === "sm" ? 15 : 17;
  return /*#__PURE__*/React.createElement("button", _extends({
    type: type,
    disabled: disabled,
    onClick: onClick,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => setHover(false),
    style: {
      cursor: disabled ? "not-allowed" : "pointer",
      borderRadius: "var(--radius-md)",
      fontFamily: "var(--font-ui)",
      fontWeight: "var(--weight-bold)",
      display: "inline-flex",
      alignItems: "center",
      justifyContent: "center",
      width: block ? "100%" : "auto",
      opacity: disabled ? 0.45 : 1,
      transition: "var(--transition)",
      whiteSpace: "nowrap",
      ...sizes[size],
      ...variants[variant],
      ...style
    }
  }, rest), icon && /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: icon,
    size: iconSize
  }), children, iconRight && /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: iconRight,
    size: iconSize
  }));
}
Object.assign(__ds_scope, { Button });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/Button.jsx", error: String((e && e.message) || e) }); }

// components/core/IconButton.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/**
 * IconButton — square rounded control button used across the conference dock.
 * variant: neutral (surface), accent (lime), danger (red), active (lime-tinted "on"),
 *          off (muted/disabled look for muted mic/cam).
 */
function IconButton({
  icon,
  variant = "neutral",
  size = "md",
  wide = false,
  label,
  disabled = false,
  onClick,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const dim = {
    sm: 40,
    md: 52,
    lg: 60
  }[size];
  const iconSize = {
    sm: 18,
    md: 22,
    lg: 26
  }[size];
  const variants = {
    neutral: {
      background: hover ? "var(--surface-2)" : "var(--surface)",
      color: "var(--text)",
      border: "1px solid var(--border)"
    },
    accent: {
      background: "var(--accent)",
      color: "var(--on-accent)",
      border: "1px solid transparent",
      filter: hover ? "brightness(1.06)" : "none"
    },
    active: {
      background: "color-mix(in srgb, var(--accent) 18%, transparent)",
      color: "var(--accent-ink)",
      border: "1px solid color-mix(in srgb, var(--accent) 45%, transparent)"
    },
    off: {
      background: hover ? "var(--surface-3)" : "var(--surface-2)",
      color: "var(--text-muted)",
      border: "1px solid var(--border)"
    },
    danger: {
      background: "var(--danger)",
      color: "var(--on-danger)",
      border: "1px solid transparent",
      filter: hover ? "brightness(1.06)" : "none"
    }
  };
  return /*#__PURE__*/React.createElement("button", _extends({
    type: "button",
    "aria-label": label,
    title: label,
    disabled: disabled,
    onClick: onClick,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => setHover(false),
    style: {
      height: `${dim}px`,
      width: wide ? `${Math.round(dim * 1.5)}px` : `${dim}px`,
      borderRadius: "var(--radius-md)",
      display: "inline-flex",
      alignItems: "center",
      justifyContent: "center",
      cursor: disabled ? "not-allowed" : "pointer",
      opacity: disabled ? 0.45 : 1,
      transition: "var(--transition)",
      ...variants[variant],
      ...style
    }
  }, rest), /*#__PURE__*/React.createElement(__ds_scope.Icon, {
    name: icon,
    size: iconSize,
    strokeWidth: 2
  }));
}
Object.assign(__ds_scope, { IconButton });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/core/IconButton.jsx", error: String((e && e.message) || e) }); }

// components/conference/ControlDock.jsx
try { (() => {
/**
 * ControlDock — the floating conference control bar. Composes IconButtons
 * for mic / camera / screen-share (optional) / settings / leave. Controlled
 * toggles; `showShare={false}` hides screen-share (mobile), `settingsOpen`
 * highlights the settings button while the settings dialog is open.
 */
function ControlDock({
  micOn = true,
  cameraOn = true,
  sharing = false,
  settingsOpen = false,
  onToggleMic,
  onToggleCamera,
  onToggleShare,
  onOpenSettings,
  onLeave,
  showShare = true,
  floating = true,
  style
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: "inline-flex",
      alignItems: "center",
      gap: "var(--gap-inline)",
      padding: "10px",
      borderRadius: "var(--radius-lg)",
      background: "var(--surface)",
      border: "1px solid var(--border)",
      boxShadow: floating ? "var(--shadow-pop)" : "none",
      fontFamily: "var(--font-ui)",
      ...style
    }
  }, /*#__PURE__*/React.createElement(__ds_scope.IconButton, {
    icon: micOn ? "mic" : "mic-off",
    variant: micOn ? "neutral" : "off",
    label: micOn ? "Выключить микрофон" : "Включить микрофон",
    onClick: onToggleMic
  }), /*#__PURE__*/React.createElement(__ds_scope.IconButton, {
    icon: cameraOn ? "video" : "video-off",
    variant: cameraOn ? "neutral" : "off",
    label: cameraOn ? "Выключить камеру" : "Включить камеру",
    onClick: onToggleCamera
  }), showShare && /*#__PURE__*/React.createElement(__ds_scope.IconButton, {
    icon: "screen",
    variant: sharing ? "active" : "neutral",
    label: "\u0414\u0435\u043C\u043E\u043D\u0441\u0442\u0440\u0430\u0446\u0438\u044F \u044D\u043A\u0440\u0430\u043D\u0430",
    onClick: onToggleShare
  }), /*#__PURE__*/React.createElement(__ds_scope.IconButton, {
    icon: "settings",
    variant: settingsOpen ? "active" : "neutral",
    label: "\u041D\u0430\u0441\u0442\u0440\u043E\u0439\u043A\u0438",
    onClick: onOpenSettings
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      width: "1px",
      alignSelf: "stretch",
      background: "var(--border)",
      margin: "0 2px"
    }
  }), /*#__PURE__*/React.createElement(__ds_scope.IconButton, {
    icon: "phone-off",
    variant: "danger",
    wide: true,
    label: "\u0417\u0430\u0432\u0435\u0440\u0448\u0438\u0442\u044C",
    onClick: onLeave
  }));
}
Object.assign(__ds_scope, { ControlDock });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/conference/ControlDock.jsx", error: String((e && e.message) || e) }); }

// components/forms/Input.jsx
try { (() => {
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
/** Input — inset field; border turns accent-ink on focus. */
function Input({
  style,
  onFocus,
  onBlur,
  ...rest
}) {
  const [focus, setFocus] = React.useState(false);
  return /*#__PURE__*/React.createElement("input", _extends({
    onFocus: e => {
      setFocus(true);
      onFocus?.(e);
    },
    onBlur: e => {
      setFocus(false);
      onBlur?.(e);
    },
    style: {
      width: "100%",
      padding: "var(--pad-input)",
      background: "var(--surface-2)",
      color: "var(--text)",
      border: `1px solid ${focus ? "var(--accent-ink)" : "var(--border)"}`,
      borderRadius: "var(--radius-md)",
      fontSize: "var(--text-md)",
      fontFamily: "var(--font-ui)",
      fontWeight: "var(--weight-medium)",
      outline: "none",
      boxSizing: "border-box",
      transition: "var(--transition)",
      ...style
    }
  }, rest));
}

/** Field — label + control, matching the lobby's stacked rows. */
function Field({
  label,
  htmlFor,
  hint,
  children,
  style
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      ...style
    }
  }, label && /*#__PURE__*/React.createElement("label", {
    htmlFor: htmlFor,
    style: {
      display: "block",
      fontFamily: "var(--font-label)",
      fontSize: "var(--text-2xs)",
      letterSpacing: "var(--tracking-label)",
      textTransform: "uppercase",
      color: "var(--text-muted)",
      margin: "0 0 8px"
    }
  }, label), children, hint && /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: "7px",
      fontSize: "var(--text-xs)",
      color: "var(--text-faint)"
    }
  }, hint));
}
Object.assign(__ds_scope, { Input, Field });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/forms/Input.jsx", error: String((e && e.message) || e) }); }

// ui_kits/web/ConferenceScreen.jsx
try { (() => {
/** ConferenceScreen — asymmetric speaker grid + side panel + floating dock. */
function ConferenceScreen({
  room,
  myName,
  theme,
  onToggleTheme,
  onLeave
}) {
  const {
    useState,
    useRef,
    useEffect
  } = React;
  const {
    Badge,
    IconButton,
    Input,
    VideoTile,
    ParticipantRow,
    ChatMessage,
    ControlDock
  } = window.MeetUpDesignSystem_2584b5;
  const PEERS = [{
    id: 1,
    name: "Дмитрий",
    muted: false
  }, {
    id: 2,
    name: "Мария",
    muted: true
  }, {
    id: 3,
    name: "Игорь",
    muted: false
  }];
  const [mic, setMic] = useState(true);
  const [cam, setCam] = useState(true);
  const [sharing, setSharing] = useState(false);
  const [tab, setTab] = useState("chat");
  const [messages, setMessages] = useState([{
    id: 1,
    author: "Мария",
    time: "14:31",
    text: "Привет! Все на месте?",
    self: false
  }, {
    id: 2,
    author: "Дмитрий",
    time: "14:32",
    text: "Да, можем начинать.",
    self: false
  }]);
  const [draft, setDraft] = useState("");
  const msgRef = useRef(null);
  const people = [{
    id: 0,
    name: myName,
    self: true,
    host: true,
    muted: !mic
  }, ...PEERS];
  useEffect(() => {
    if (msgRef.current) msgRef.current.scrollTop = msgRef.current.scrollHeight;
  }, [messages, tab]);
  function send() {
    const t = draft.trim();
    if (!t) return;
    const time = new Date().toLocaleTimeString("ru-RU", {
      hour: "2-digit",
      minute: "2-digit"
    });
    setMessages(m => [...m, {
      id: Date.now(),
      author: "Вы",
      time,
      text: t,
      self: true
    }]);
    setDraft("");
  }
  return /*#__PURE__*/React.createElement("div", {
    style: {
      position: "absolute",
      inset: 0,
      background: "var(--bg)",
      color: "var(--text)",
      display: "grid",
      gridTemplateColumns: "1fr 340px",
      fontFamily: "var(--font-ui)"
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: "flex",
      flexDirection: "column",
      minWidth: 0,
      position: "relative"
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: "flex",
      alignItems: "center",
      gap: 14,
      padding: "20px 26px"
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      fontFamily: "var(--font-label)",
      fontWeight: 700,
      fontSize: 15,
      letterSpacing: 2,
      textTransform: "uppercase",
      color: "var(--text-muted)"
    }
  }, "MeetUp"), /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      fontFamily: "var(--font-display)",
      fontWeight: 700,
      fontSize: 24,
      letterSpacing: "-.8px"
    }
  }, "\u0414\u0438\u0437\u0430\u0439\u043D-", /*#__PURE__*/React.createElement("span", {
    style: {
      color: "var(--accent-ink)"
    }
  }, "\u0441\u0438\u043D\u043A")), /*#__PURE__*/React.createElement("span", {
    style: {
      flex: 1
    }
  }), /*#__PURE__*/React.createElement(Badge, {
    tone: "live",
    dot: true
  }, "\u0432 \u044D\u0444\u0438\u0440\u0435 \xB7 04:12"), /*#__PURE__*/React.createElement(Badge, {
    tone: "accent"
  }, room), /*#__PURE__*/React.createElement(IconButton, {
    icon: theme === "dark" ? "sun" : "moon",
    size: "sm",
    label: "\u0421\u043C\u0435\u043D\u0438\u0442\u044C \u0442\u0435\u043C\u0443",
    onClick: onToggleTheme
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minHeight: 0,
      display: "grid",
      gap: "var(--gap-grid)",
      gridTemplateColumns: "2fr 1fr",
      gridTemplateRows: "1fr 1fr",
      padding: "4px 26px 120px"
    }
  }, /*#__PURE__*/React.createElement(VideoTile, {
    name: myName,
    self: true,
    speaking: mic,
    muted: !mic,
    style: {
      gridRow: "1 / 3",
      aspectRatio: "auto",
      height: "100%"
    }
  }), /*#__PURE__*/React.createElement(VideoTile, {
    name: "\u0414\u043C\u0438\u0442\u0440\u0438\u0439",
    style: {
      aspectRatio: "auto",
      height: "100%"
    }
  }), /*#__PURE__*/React.createElement(VideoTile, {
    name: "\u041C\u0430\u0440\u0438\u044F",
    muted: true,
    style: {
      aspectRatio: "auto",
      height: "100%"
    }
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      position: "absolute",
      left: "50%",
      bottom: 28,
      transform: "translateX(-50%)"
    }
  }, /*#__PURE__*/React.createElement(ControlDock, {
    micOn: mic,
    cameraOn: cam,
    sharing: sharing,
    onToggleMic: () => setMic(v => !v),
    onToggleCamera: () => setCam(v => !v),
    onToggleShare: () => setSharing(v => !v),
    onLeave: onLeave
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      borderLeft: "1px solid var(--border)",
      background: "var(--surface)",
      display: "flex",
      flexDirection: "column",
      minHeight: 0
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: "flex",
      gap: 4,
      padding: "16px 16px 0"
    }
  }, [["chat", "Чат", "chat"], ["people", "Участники", "users"]].map(([k, label]) => /*#__PURE__*/React.createElement("button", {
    key: k,
    onClick: () => setTab(k),
    style: {
      flex: 1,
      padding: "10px",
      borderRadius: "var(--radius-md)",
      cursor: "pointer",
      border: "1px solid " + (tab === k ? "transparent" : "var(--border)"),
      background: tab === k ? "var(--accent)" : "transparent",
      color: tab === k ? "var(--on-accent)" : "var(--text-muted)",
      fontFamily: "var(--font-ui)",
      fontWeight: 700,
      fontSize: 13
    }
  }, label))), tab === "people" ? /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      overflow: "auto",
      padding: "12px 8px"
    }
  }, people.map(p => /*#__PURE__*/React.createElement(ParticipantRow, {
    key: p.id,
    name: p.name,
    you: p.self,
    host: p.host,
    muted: p.muted
  }))) : /*#__PURE__*/React.createElement(React.Fragment, null, /*#__PURE__*/React.createElement("div", {
    ref: msgRef,
    style: {
      flex: 1,
      overflow: "auto",
      padding: "16px 16px",
      display: "flex",
      flexDirection: "column",
      gap: 14
    }
  }, messages.map(m => /*#__PURE__*/React.createElement(ChatMessage, {
    key: m.id,
    author: m.author,
    time: m.time,
    text: m.text,
    self: m.self
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: "flex",
      gap: 8,
      padding: 14,
      borderTop: "1px solid var(--border)"
    }
  }, /*#__PURE__*/React.createElement(Input, {
    value: draft,
    placeholder: "\u0421\u043E\u043E\u0431\u0449\u0435\u043D\u0438\u0435\u2026",
    autoComplete: "off",
    onChange: e => setDraft(e.target.value),
    onKeyDown: e => e.key === "Enter" && send()
  }), /*#__PURE__*/React.createElement(IconButton, {
    icon: "send",
    variant: "accent",
    label: "\u041E\u0442\u043F\u0440\u0430\u0432\u0438\u0442\u044C",
    onClick: send
  })))));
}
window.ConferenceScreen = ConferenceScreen;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/web/ConferenceScreen.jsx", error: String((e && e.message) || e) }); }

// ui_kits/web/LobbyScreen.jsx
try { (() => {
/** LobbyScreen — the entry view. Big display headline + join card. */
function LobbyScreen({
  onJoin,
  theme,
  onToggleTheme
}) {
  const {
    useState
  } = React;
  const {
    Card,
    Button,
    Input,
    Field,
    IconButton
  } = window.MeetUpDesignSystem_2584b5;
  const [room, setRoom] = useState("DESIGN-01");
  const [name, setName] = useState("");
  const [err, setErr] = useState("");
  function submit() {
    if (!room.trim() || !name.trim()) {
      setErr("Введите код комнаты и имя.");
      return;
    }
    onJoin(room.trim(), name.trim());
  }
  return /*#__PURE__*/React.createElement("div", {
    style: {
      position: "absolute",
      inset: 0,
      background: "var(--bg)",
      color: "var(--text)",
      display: "flex",
      flexDirection: "column",
      fontFamily: "var(--font-ui)"
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: "flex",
      alignItems: "center",
      padding: "22px 28px"
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      fontFamily: "var(--font-label)",
      fontWeight: 700,
      fontSize: 18,
      letterSpacing: 2,
      textTransform: "uppercase"
    }
  }, "MeetUp"), /*#__PURE__*/React.createElement("span", {
    style: {
      flex: 1
    }
  }), /*#__PURE__*/React.createElement(IconButton, {
    icon: theme === "dark" ? "sun" : "moon",
    size: "sm",
    label: "\u0421\u043C\u0435\u043D\u0438\u0442\u044C \u0442\u0435\u043C\u0443",
    onClick: onToggleTheme
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      display: "grid",
      placeItems: "center",
      padding: "0 28px 40px"
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: "grid",
      gridTemplateColumns: "1.1fr .9fr",
      gap: 48,
      alignItems: "center",
      maxWidth: 880,
      width: "100%"
    }
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement("h1", {
    style: {
      margin: 0,
      fontFamily: "var(--font-display)",
      fontWeight: 800,
      fontSize: "clamp(38px, 6vw, 60px)",
      lineHeight: .95,
      letterSpacing: "-1.5px"
    }
  }, "\u0412\u0441\u0442\u0440\u0435\u0447\u0430", /*#__PURE__*/React.createElement("br", null), "\u0437\u0430 ", /*#__PURE__*/React.createElement("span", {
    style: {
      color: "var(--accent-ink)"
    }
  }, "\u043E\u0434\u0438\u043D \u043A\u043B\u0438\u043A")), /*#__PURE__*/React.createElement("p", {
    style: {
      marginTop: 20,
      maxWidth: 340,
      color: "var(--text-muted)",
      fontSize: 15,
      lineHeight: 1.5
    }
  }, "\u041E\u0442\u043A\u0440\u044B\u0442\u0430\u044F \u0432\u0438\u0434\u0435\u043E\u0441\u0432\u044F\u0437\u044C \u0431\u0435\u0437 \u0443\u0441\u0442\u0430\u043D\u043E\u0432\u043A\u0438. \u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u043A\u043E\u0434 \u043A\u043E\u043C\u043D\u0430\u0442\u044B \u2014 \u0438 \u0432\u044B \u0432 \u044D\u0444\u0438\u0440\u0435.")), /*#__PURE__*/React.createElement(Card, {
    elevated: true,
    width: "100%",
    style: {
      maxWidth: 360,
      justifySelf: "end"
    }
  }, /*#__PURE__*/React.createElement(Field, {
    label: "\u041A\u043E\u0434 \u043A\u043E\u043C\u043D\u0430\u0442\u044B",
    htmlFor: "room",
    hint: "\u0418\u043B\u0438 \u0441\u043E\u0437\u0434\u0430\u0439\u0442\u0435 \u043D\u043E\u0432\u0443\u044E \u043A\u043E\u043C\u043D\u0430\u0442\u0443"
  }, /*#__PURE__*/React.createElement(Input, {
    id: "room",
    value: room,
    autoComplete: "off",
    onChange: e => {
      setRoom(e.target.value);
      setErr("");
    }
  })), /*#__PURE__*/React.createElement(Field, {
    label: "\u0412\u0430\u0448\u0435 \u0438\u043C\u044F",
    htmlFor: "name",
    style: {
      marginTop: 18
    }
  }, /*#__PURE__*/React.createElement(Input, {
    id: "name",
    value: name,
    autoComplete: "off",
    placeholder: "\u0410\u043D\u043D\u0430",
    onChange: e => {
      setName(e.target.value);
      setErr("");
    },
    onKeyDown: e => e.key === "Enter" && submit()
  })), /*#__PURE__*/React.createElement(Button, {
    variant: "primary",
    block: true,
    iconRight: "arrow-right",
    style: {
      marginTop: 22
    },
    onClick: submit
  }, "\u0412\u043E\u0439\u0442\u0438 \u0432 \u043A\u043E\u043D\u0444\u0435\u0440\u0435\u043D\u0446\u0438\u044E"), /*#__PURE__*/React.createElement(Button, {
    variant: "ghost",
    block: true,
    icon: "plus",
    style: {
      marginTop: 10
    },
    onClick: () => onJoin("NEW-" + Math.random().toString(36).slice(2, 6).toUpperCase(), name.trim() || "Гость")
  }, "\u0421\u043E\u0437\u0434\u0430\u0442\u044C \u043A\u043E\u043C\u043D\u0430\u0442\u0443"), /*#__PURE__*/React.createElement("div", {
    style: {
      marginTop: 14,
      textAlign: "center",
      fontSize: 12,
      color: err ? "var(--danger)" : "var(--text-faint)"
    }
  }, err || "Демо: данные не отправляются на сервер.")))));
}
window.LobbyScreen = LobbyScreen;
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/web/LobbyScreen.jsx", error: String((e && e.message) || e) }); }

__ds_ns.ChatMessage = __ds_scope.ChatMessage;

__ds_ns.ControlDock = __ds_scope.ControlDock;

__ds_ns.ParticipantRow = __ds_scope.ParticipantRow;

__ds_ns.VideoTile = __ds_scope.VideoTile;

__ds_ns.Badge = __ds_scope.Badge;

__ds_ns.Button = __ds_scope.Button;

__ds_ns.Card = __ds_scope.Card;

__ds_ns.Icon = __ds_scope.Icon;

__ds_ns.IconButton = __ds_scope.IconButton;

__ds_ns.Input = __ds_scope.Input;

__ds_ns.Field = __ds_scope.Field;

})();
