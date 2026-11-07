// A small demonstration of the management API over HTTPS. It makes the same requests as the curl commands of
// docs/ssh-integrated-image.md, so that a person can watch them in a browser. Not a product frontend.
"use strict";

const API = "/api/v1";
const $ = (id) => document.getElementById(id);

function cookie(name) {
  for (const part of document.cookie.split("; ")) {
    if (part.startsWith(name + "=")) {
      return part.slice(name.length + 1);
    }
  }
  return "";
}

async function call(method, path, body) {
  const headers = { "X-Crux-Request": "1" };
  if (body !== undefined) {
    headers["Content-Type"] = "application/json";
  }
  // The token cookie exists once logged in. The login itself is public and needs none.
  if (method !== "GET" && cookie("XSRF-TOKEN") !== "") {
    headers["X-XSRF-TOKEN"] = cookie("XSRF-TOKEN");
  }
  const reply = await fetch(API + path, {
    method,
    headers,
    credentials: "same-origin",
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  let json = null;
  try {
    json = await reply.json();
  } catch (error) {
    json = null;
  }
  return { status: reply.status, json };
}

async function login(user, password) {
  const started = await call("POST", "/session", { user, password });
  if (started.status !== 202) {
    return "refused: " + started.status;
  }
  for (let attempt = 0; attempt < 300; ++attempt) {
    const state = await call("GET", "/session/attempt/" + started.json.id);
    if (state.json && state.json.state === "pending") {
      await new Promise((resolve) => setTimeout(resolve, 200));
      continue;
    }
    if (state.json && state.json.state === "success") {
      return "";
    }
    return "refused: " + state.status + " " + JSON.stringify(state.json);
  }
  return "timed out";
}

async function refresh() {
  const status = await call("GET", "/status");
  const rows = $("status").tBodies[0];
  rows.replaceChildren();
  for (const item of (status.json && status.json.items) || []) {
    const row = rows.insertRow();
    row.insertCell().textContent = item.name;
    row.insertCell().textContent = String(item.value);
  }
  const name = await call("GET", "/config/demo.display_name");
  $("name").value = name.json && name.json.value !== undefined ? name.json.value : "";
}

async function show() {
  const me = await call("GET", "/session/me");
  const loggedIn = me.status === 200;
  $("login").hidden = loggedIn;
  $("session").hidden = !loggedIn;
  if (loggedIn) {
    $("who").textContent = me.json.user;
    await refresh();
  }
}

$("login").addEventListener("submit", async (event) => {
  event.preventDefault();
  $("login-result").textContent = "logging in";
  $("login-result").textContent = await login($("user").value, $("password").value);
  $("password").value = "";
  await show();
});
$("logout").addEventListener("click", async () => {
  await call("DELETE", "/session");
  await show();
});
$("refresh").addEventListener("click", refresh);
$("save").addEventListener("click", async () => {
  const result = await call("PUT", "/config/demo.display_name", { value: $("name").value });
  $("save-result").textContent = result.status === 200 ? "saved" : "refused: " + result.status;
  await refresh();
});
show();
